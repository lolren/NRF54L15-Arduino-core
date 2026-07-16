#!/usr/bin/env python3
"""Direct-RRAM Zigbee persistence journal contracts and fault model."""

from __future__ import annotations

import binascii
import pathlib
import struct


ROOT = pathlib.Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
SOURCE_PATH = (
    PLATFORM
    / "libraries/Nrf54L15-Clean-Implementation/src/zigbee_persistence.cpp"
)

PAGE_LEN = 4096
PARTITION_COUNT = 2
SLOTS_PER_PARTITION = 2
SLOT_COUNT = PARTITION_COUNT * SLOTS_PER_PARTITION
SLOT_LEN = PAGE_LEN // SLOT_COUNT
HEADER_LEN = 64
COMMIT_OFFSET = 48
MAGIC = 0x5A424A52
FORMAT_VERSION = 1
PAYLOAD_VERSION = 1
COMMIT = 0x434F4D54
LIVE = 1
TOMBSTONE = 2


class SimulatedPowerLoss(RuntimeError):
    pass


class FaultRram:
    """Write-buffered RRAM model; only commit makes pending writes durable."""

    def __init__(self, durable: bytes | None = None) -> None:
        self.durable = bytearray(durable or (b"\xFF" * PAGE_LEN))
        self.pending: list[tuple[int, bytes]] = []
        self.fail_at: int | None = None
        self.phase = 0
        self.trace: list[str] = []

    def arm(self, fail_at: int | None) -> None:
        self.fail_at = fail_at
        self.phase = 0
        self.trace.clear()
        self.pending.clear()

    def _phase(self, label: str) -> None:
        if self.phase == self.fail_at:
            self.pending.clear()
            raise SimulatedPowerLoss(label)
        self.trace.append(label)
        self.phase += 1

    def write(self, offset: int, data: bytes, label: str) -> None:
        self._phase(f"write:{label}")
        assert 0 <= offset <= PAGE_LEN and len(data) <= PAGE_LEN - offset
        self.pending.append((offset, bytes(data)))

    def commit(self, label: str) -> None:
        self._phase(f"commit:{label}")
        for offset, data in self.pending:
            self.durable[offset : offset + len(data)] = data
        self.pending.clear()

    def power_loss(self) -> None:
        self.pending.clear()


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def name_hash(name: str) -> int:
    raw = name.encode("ascii")
    assert 0 < len(raw) <= 15
    value = 2166136261
    for byte in raw:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def partition_slots(name: str) -> tuple[int, int]:
    first = (name_hash(name) % PARTITION_COUNT) * SLOTS_PER_PARTITION
    return first, first + 1


def encode_header(
    name: str,
    generation: int,
    state: int,
    payload: bytes,
    committed: bool,
) -> bytes:
    raw_name = name.encode("ascii")
    assert 0 < len(raw_name) <= 15
    header = bytearray(HEADER_LEN)
    struct.pack_into("<IHHIIIII", header, 0, MAGIC, FORMAT_VERSION,
                     PAYLOAD_VERSION, name_hash(name), generation & 0xFFFFFFFF,
                     len(payload), crc32(payload), state)
    header[28] = len(raw_name)
    header[29 : 29 + len(raw_name)] = raw_name
    struct.pack_into("<I", header, 44, crc32(header[:44]))
    struct.pack_into("<I", header, COMMIT_OFFSET, COMMIT if committed else 0)
    return bytes(header)


def read_slot(rram: FaultRram, slot: int) -> dict[str, object] | None:
    offset = slot * SLOT_LEN
    header = bytes(rram.durable[offset : offset + HEADER_LEN])
    try:
        magic, fmt, payload_version, stored_hash, generation, length, payload_crc, state = (
            struct.unpack_from("<IHHIIIII", header, 0)
        )
        header_crc = struct.unpack_from("<I", header, 44)[0]
        commit = struct.unpack_from("<I", header, COMMIT_OFFSET)[0]
    except struct.error:
        return None
    name_len = header[28]
    if (
        magic != MAGIC
        or fmt != FORMAT_VERSION
        or payload_version != PAYLOAD_VERSION
        or commit != COMMIT
        or header_crc != crc32(header[:44])
        or not (0 < name_len <= 15)
        or any(header[29 + name_len : 44])
        or any(header[52:])
    ):
        return None
    try:
        name = header[29 : 29 + name_len].decode("ascii")
    except UnicodeDecodeError:
        return None
    if name_hash(name) != stored_hash:
        return None
    if state == LIVE:
        if not (0 < length <= 512):
            return None
        payload = bytes(
            rram.durable[offset + HEADER_LEN : offset + HEADER_LEN + length]
        )
        if crc32(payload) != payload_crc:
            return None
    elif state == TOMBSTONE:
        if length != 0 or payload_crc != 0:
            return None
        payload = b""
    else:
        return None
    return {
        "slot": slot,
        "name": name,
        "generation": generation,
        "state": state,
        "payload": payload,
    }


def newer(candidate: int, reference: int) -> bool:
    delta = (candidate - reference) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def newest(rram: FaultRram, name: str) -> dict[str, object] | None:
    matches = [
        record
        for slot in partition_slots(name)
        if (record := read_slot(rram, slot)) is not None
        and record["name"] == name
    ]
    if not matches:
        return None
    if len(matches) == 1:
        return matches[0]
    return matches[1] if newer(
        int(matches[1]["generation"]), int(matches[0]["generation"])
    ) else matches[0]


def load(rram: FaultRram, name: str) -> bytes | None:
    record = newest(rram, name)
    if record is None or record["state"] == TOMBSTONE:
        return None
    return bytes(record["payload"])


def choose_target(rram: FaultRram, name: str,
                  allow_namespace_replacement: bool = False) -> tuple[int, int]:
    pair = partition_slots(name)
    slots = (read_slot(rram, pair[0]), read_slot(rram, pair[1]))
    if (any(record is not None and record["name"] != name for record in slots)
            and not allow_namespace_replacement):
        raise ValueError("namespace partition collision")
    current = newest(rram, name)
    if current is not None:
        target = pair[1] if int(current["slot"]) == pair[0] else pair[0]
        return target, (int(current["generation"]) + 1) & 0xFFFFFFFF
    if slots[0] is None:
        return pair[0], 1
    if slots[1] is None:
        return pair[1], 1
    target = 1 if newer(int(slots[0]["generation"]), int(slots[1]["generation"])) else 0
    return pair[target], 1


def write_record(rram: FaultRram, name: str, state: int, payload: bytes,
                 allow_namespace_replacement: bool = False) -> None:
    slot, generation = choose_target(rram, name, allow_namespace_replacement)
    offset = slot * SLOT_LEN
    uncommitted = encode_header(name, generation, state, payload, False)
    rram.write(offset + COMMIT_OFFSET, b"\0\0\0\0", "invalidate")
    rram.commit("invalidate")
    if state == LIVE:
        rram.write(offset + HEADER_LEN, payload, "payload")
    else:
        rram.write(offset + HEADER_LEN, bytes(SLOT_LEN - HEADER_LEN),
                   "payload_wipe")
    rram.write(offset, uncommitted, "header")
    rram.commit("body")
    rram.write(offset + COMMIT_OFFSET, struct.pack("<I", COMMIT), "marker")
    rram.commit("marker")
    record = read_slot(rram, slot)
    assert record is not None and record["payload"] == payload
    if state == TOMBSTONE:
        assert not any(rram.durable[offset + HEADER_LEN : offset + SLOT_LEN])


def seed_record(rram: FaultRram, slot: int, name: str, generation: int,
                state: int, payload: bytes) -> None:
    offset = slot * SLOT_LEN
    rram.durable[offset : offset + HEADER_LEN] = encode_header(
        name, generation, state, payload, True
    )
    rram.durable[offset + HEADER_LEN : offset + HEADER_LEN + len(payload)] = payload


def validate_save_faults() -> None:
    old = b"old-state-and-counters"
    new = b"new-state-and-higher-counters" * 7
    baseline = FaultRram()
    pair = partition_slots("zigbee")
    seed_record(baseline, pair[0], "zigbee", 22, LIVE, old)

    complete = FaultRram(baseline.durable)
    complete.arm(None)
    write_record(complete, "zigbee", LIVE, new)
    phases = complete.phase
    assert complete.trace == [
        "write:invalidate", "commit:invalidate", "write:payload",
        "write:header", "commit:body", "write:marker", "commit:marker",
    ]
    assert load(complete, "zigbee") == new

    for fail_at in range(phases):
        trial = FaultRram(baseline.durable)
        trial.arm(fail_at)
        try:
            write_record(trial, "zigbee", LIVE, new)
        except SimulatedPowerLoss:
            trial.power_loss()
        assert load(trial, "zigbee") in (old, new), fail_at
    print(f"PASS live update survives all {phases} RRAM write/commit phases")


def validate_corruption_and_wrap() -> None:
    rram = FaultRram()
    pair = partition_slots("zigbee")
    seed_record(rram, pair[0], "zigbee", 9, LIVE, b"old")
    seed_record(rram, pair[1], "zigbee", 10, LIVE, b"new")
    assert load(rram, "zigbee") == b"new"

    payload_corrupt = FaultRram(rram.durable)
    payload_corrupt.durable[pair[1] * SLOT_LEN + HEADER_LEN] ^= 0x80
    assert load(payload_corrupt, "zigbee") == b"old"

    header_corrupt = FaultRram(rram.durable)
    header_corrupt.durable[pair[1] * SLOT_LEN + 12] ^= 0x01
    assert load(header_corrupt, "zigbee") == b"old"

    marker_corrupt = FaultRram(rram.durable)
    marker_corrupt.durable[pair[1] * SLOT_LEN + COMMIT_OFFSET] ^= 0x01
    assert load(marker_corrupt, "zigbee") == b"old"

    wrapped = FaultRram()
    seed_record(wrapped, pair[0], "zigbee", 0xFFFFFFFF, LIVE, b"before-wrap")
    seed_record(wrapped, pair[1], "zigbee", 0, LIVE, b"after-wrap")
    assert load(wrapped, "zigbee") == b"after-wrap"
    print("PASS independent header/payload/marker CRC checks and generation wrap")


def validate_tombstone_faults() -> None:
    baseline = FaultRram()
    pair = partition_slots("zigbee")
    seed_record(baseline, pair[0], "zigbee", 5, LIVE, b"joined-network")

    complete = FaultRram(baseline.durable)
    complete.arm(None)
    write_record(complete, "zigbee", TOMBSTONE, b"")
    phases = complete.phase
    assert load(complete, "zigbee") is None

    for fail_at in range(phases):
        trial = FaultRram(baseline.durable)
        trial.arm(fail_at)
        try:
            write_record(trial, "zigbee", TOMBSTONE, b"")
        except SimulatedPowerLoss:
            trial.power_loss()
        assert load(trial, "zigbee") in (b"joined-network", None), fail_at

    # The production clear() repeats this write, leaving two tombstone replicas.
    write_record(complete, "zigbee", TOMBSTONE, b"")
    assert read_slot(complete, pair[0])["state"] == TOMBSTONE
    assert read_slot(complete, pair[1])["state"] == TOMBSTONE
    for slot in pair:
        offset = slot * SLOT_LEN
        assert not any(complete.durable[offset + HEADER_LEN : offset + SLOT_LEN])
    print(f"PASS tombstone clear survives all {phases} RRAM phases and replicates")


def validate_namespace_and_legacy_migration() -> None:
    rram = FaultRram()
    pair_a = partition_slots("zigbee-a")
    pair_b = partition_slots("zigbee-b")
    assert pair_a != pair_b
    seed_record(rram, pair_a[0], "zigbee-a", 1, LIVE, b"network-a")
    seed_record(rram, pair_b[0], "zigbee-b", 1, LIVE, b"network-b")
    assert load(rram, "zigbee-a") == b"network-a"
    assert load(rram, "zigbee-b") == b"network-b"
    assert load(rram, "zigbee-c") is None

    # Updating one namespace through both A/B slots must never evict the other.
    write_record(rram, "zigbee-a", LIVE, b"network-a-2")
    write_record(rram, "zigbee-a", LIVE, b"network-a-3")
    assert load(rram, "zigbee-a") == b"network-a-3"
    assert load(rram, "zigbee-b") == b"network-b"

    update_baseline = bytes(rram.durable)
    probe = FaultRram(update_baseline)
    probe.arm(None)
    write_record(probe, "zigbee-a", LIVE, b"network-a-4")
    for fail_at in range(probe.phase):
        trial = FaultRram(update_baseline)
        trial.arm(fail_at)
        try:
            write_record(trial, "zigbee-a", LIVE, b"network-a-4")
        except SimulatedPowerLoss:
            trial.power_loss()
        assert load(trial, "zigbee-b") == b"network-b"
        assert load(trial, "zigbee-a") in (b"network-a-3", b"network-a-4")

    collision = FaultRram()
    assert partition_slots("alpha") == partition_slots("beta")
    seed_record(collision, partition_slots("alpha")[0], "alpha", 1,
                LIVE, b"alpha-state")
    try:
        write_record(collision, "beta", LIVE, b"beta-state")
        raise AssertionError("colliding namespace write unexpectedly succeeded")
    except ValueError:
        pass
    assert load(collision, "alpha") == b"alpha-state"
    # Explicit factory clear may reclaim a colliding partition, but ordinary
    # live saves above must never evict it implicitly.
    write_record(collision, "beta", TOMBSTONE, b"",
                 allow_namespace_replacement=True)
    write_record(collision, "beta", TOMBSTONE, b"",
                 allow_namespace_replacement=True)
    assert load(collision, "beta") is None

    legacy = {"state": b"legacy-v6", "stlen": b"length"}
    migrated = FaultRram()
    migrated.arm(None)
    write_record(migrated, "zigbee", LIVE, legacy["state"])
    # Once direct state commits, any partial legacy cleanup is harmless.
    assert load(FaultRram(migrated.durable), "zigbee") == b"legacy-v6"
    del legacy["state"]
    assert load(FaultRram(migrated.durable), "zigbee") == b"legacy-v6"
    del legacy["stlen"]
    assert load(migrated, "zigbee") == b"legacy-v6" and not legacy

    for fail_at in range(7):
        trial = FaultRram()
        old = {"state": b"legacy-v6", "stlen": b"length"}
        trial.arm(fail_at)
        try:
            write_record(trial, "zigbee", LIVE, old["state"])
        except SimulatedPowerLoss:
            trial.power_loss()
        # Direct migration failure never performs the later Preferences cleanup.
        if load(trial, "zigbee") is None:
            assert old["state"] == b"legacy-v6"
    print("PASS exact namespace isolation and commit-before-legacy-cleanup model")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(signature)


def validate_source_and_layout_contracts() -> None:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    for token in (
        'section(".zigbee_storage")',
        "g_zigbeeStoragePage[kJournalPageLen]",
        "kJournalPageLen = 4096U",
        "kJournalSlotCount =",
        "kJournalSlotLen = kJournalPageLen / kJournalSlotCount",
        "kSerializedJournalPayloadLenV6 = 336U",
        "kSerializedJournalPayloadLen = 464U",
        "nrf54l15_rram_transaction_try_lock",
        "TASKS_COMMITWRITEBUF",
        "waitRramcReadyNext",
        "serializePersistentState",
        "deserializePersistentState",
        "namespaceHash",
        "delta < 0x80000000UL",
        "kJournalStateTombstone",
        "Only a committed/read-back-verified",
        "struct ZigbeePersistentStateV1",
        "struct ZigbeePersistentStateV2",
        "struct ZigbeePersistentStateV3",
        "struct ZigbeePersistentStateV4",
        "struct ZigbeePersistentStateV5",
        "struct ZigbeePersistentStateV6",
        "ZigbeePersistentPeerSecurityState",
        "parsed.version = kZigbeeStateVersion",
        "loadChunkedState",
    ):
        assert token in source, token

    writer = function_body(source, "bool writeJournalRecord(")
    invalid = writer.index("invalidCommit")
    first_commit = writer.index("session->commit()", invalid)
    header = writer.index("session->write(slotOffset, header", first_commit)
    body_commit = writer.index("session->commit()", header)
    marker = writer.index("validCommit", body_commit)
    final_commit = writer.index("session->commit()", marker)
    verify = writer.index("readJournalSlot(slot, &verified)", final_commit)
    assert invalid < first_commit < header < body_commit < marker < final_commit < verify

    transaction = function_body(source, "bool commitStateForNamespaceLocked(")
    scan = transaction.index("readJournalSlot(firstSlot")
    write = transaction.index("writeJournalRecord(")
    readback = transaction.index("readJournalSlot(firstSlot", write)
    assert scan < write < readback
    assert "slotAForeign || slotBForeign" in transaction
    wrapper = function_body(source, "bool commitStateForNamespace(")
    assert wrapper.index("ZigbeeRramWriteSession session;") < wrapper.index(
        "commitStateForNamespaceLocked(")

    loader = function_body(source, "bool ZigbeePersistentStateStore::load(")
    assert loader.index("newestValidSlotForNamespace(") < loader.index(
        "kPrefsKeyLegacyState"
    )
    assert loader.index("commitStateForNamespace(") < loader.index(
        "clearLegacyStorage("
    )

    clearer = function_body(source, "bool ZigbeePersistentStateStore::clear(")
    assert clearer.count("kJournalStateTombstone") == 2
    assert clearer.count("nullptr, true") == 2
    assert "bodyLength = kJournalSlotLen - kJournalHeaderLen" in writer

    serializer = function_body(source, "bool serializePersistentState(")
    deserializer = function_body(source, "bool deserializePersistentState(")
    validator = function_body(
        source, "bool ZigbeePersistentStateStore::isValid("
    )
    for token in (
        "canonical.peerSecurity",
        "peer.incomingNwkFrameCounter",
        "peer.networkKeySequence",
    ):
        assert token in serializer
    assert "payloadLen == kSerializedJournalPayloadLenV6" in deserializer
    assert "parsed.version != 6U" in deserializer
    assert "parsed.peerSecurity" in deserializer
    assert "peer.ieeeAddress == 0U" in validator
    assert "peer.shortAddress == 0xFFFFU" in validator

    linker_contracts = (
        ("cores/nrf54l15/nrf54l15_linker_script.ld", "0x17A000", "0x0017B000", "0x0017C000"),
        ("cores/nrf54l15/nrf54l15_linker_script_no_vpr.ld", "0x17A000", "0x0017B000", "0x0017C000"),
        ("cores/nrf54l15/nrf54lm20b_linker_script.ld", "0x1FA000", "0x001FB000", "0x001FC000"),
        ("cores/nrf54lm20b/nrf54lm20b_linker_script.ld", "0x1FA000", "0x001FB000", "0x001FC000"),
    )
    for relative, zigbee, bond_db, bond in linker_contracts:
        linker = (PLATFORM / relative).read_text(encoding="utf-8")
        assert f"LENGTH = {zigbee}" in linker
        assert f"FLASH_ZIGBEE (r) : ORIGIN = 0x00{zigbee[2:]}" in linker
        assert f"FLASH_BOND_DB (r) : ORIGIN = {bond_db}" in linker
        assert f"FLASH_BOND (r)  : ORIGIN = {bond}" in linker
        assert ".zigbee_storage ORIGIN(FLASH_ZIGBEE) (NOLOAD)" in linker

    boards = (PLATFORM / "boards.txt").read_text(encoding="utf-8")
    assert boards.count("upload.maximum_size=1548288") == 5
    assert boards.count("upload.maximum_size=2072576") == 1
    print("PASS direct RRAM source ordering, linker reservation, and board boundaries")


def main() -> None:
    validate_save_faults()
    validate_corruption_and_wrap()
    validate_tombstone_faults()
    validate_namespace_and_legacy_migration()
    validate_source_and_layout_contracts()
    print("PASS all Zigbee direct-RRAM persistence contracts")


if __name__ == "__main__":
    main()
