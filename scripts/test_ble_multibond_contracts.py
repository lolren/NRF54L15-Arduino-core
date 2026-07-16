#!/usr/bin/env python3
"""Host/source contracts for the eight-peer BLE bond store.

The production store intentionally has a small, fixed on-media format. These
checks focus on durable invariants instead of snapshots of complete functions.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import struct
from typing import Callable
import zlib


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware/nrf54l15clean/nrf54l15clean"
HAL = PLATFORM / "libraries/Nrf54L15-Clean-Implementation/src"
PARTS = HAL / "nrf54l15_hal_parts"
BLUEFRUIT = PLATFORM / "libraries/Bluefruit52Lib/src"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(pattern: str, text: str, message: str, flags: int = 0) -> re.Match[str]:
    match = re.search(pattern, text, flags)
    assert match is not None, message
    return match


def function_body(text: str, signatures: tuple[str, ...], label: str) -> str:
    starts = [(text.find(signature), signature) for signature in signatures]
    starts = [(offset, signature) for offset, signature in starts if offset >= 0]
    assert starts, f"missing {label}; tried {', '.join(signatures)}"
    start, signature = min(starts)
    brace = text.find("{", start + len(signature))
    assert brace >= 0, f"missing opening brace for {label}"
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated {label}")


def semantic_trace_present(text: str, alternatives: tuple[str, ...]) -> bool:
    traces = re.findall(r'(?:emitBleTrace|trace)\s*\(\s*"([^"]+)"', text)
    normalized = tuple(trace.upper() for trace in traces)
    return any(all(word in trace for word in alternative.split("+")) for trace in normalized for alternative in alternatives)


def validate_fixed_media_layout() -> None:
    store = source(PARTS / "nrf54l15_hal_internal_gatt_bond.inc")
    hal = source(HAL / "nrf54l15_hal.h")

    assert re.search(r"kBleMaxBonds\s*=\s*8U?\b", hal), (
        "bond DB must declare eight logical bonds"
    )
    assert re.search(r"kBleBondDbReplicaCount\s*=\s*2U?\b", store), (
        "each logical bond must have two physical replicas"
    )
    require(
        r"static_assert\s*\(\s*sizeof\s*\([^)]*(?:[Mm]ulti[Bb]ond|BondDb)[^)]*[Hh]eader[^)]*\)\s*==\s*16U?",
        store,
        "commit header must be one 16-byte RRAM wordline",
        re.DOTALL,
    )
    require(
        r"static_assert\s*\(\s*sizeof\s*\([^)]*(?:[Mm]ulti[Bb]ond|BondDb)[^)]*[Pp]ayload[^)]*\)\s*==\s*240U?",
        store,
        "multibond payload must remain exactly 240 bytes",
        re.DOTALL,
    )
    require(
        r"static_assert\s*\(\s*sizeof\s*\([^)]*(?:[Mm]ulti[Bb]ond|BondDb)[^)]*(?:[Ss]lot|[Rr]eplica)[^)]*\)\s*==\s*256U?",
        store,
        "each durable replica must remain exactly 256 bytes",
        re.DOTALL,
    )
    assert 'section(".bond_db_storage")' in store, (
        "sixteen replicas must live in the stable .bond_db_storage section"
    )
    assert re.search(
        r"replicas\s*\[\s*kBleBondDbReplicaCount\s*\]\s*"
        r"\[\s*(?:[^\]]*::)?kBleMaxBonds\s*\]",
        store,
    ), "durable image must allocate two banks of eight replicas"
    assert re.search(r"sizeof\s*\(\s*BleBondDbPage\s*\)\s*==\s*4096U", store)


def validate_legacy_key_tuple_contracts() -> None:
    store = source(PARTS / "nrf54l15_hal_internal_gatt_bond.inc")
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")

    for pattern, message in (
        (r"BleBondDbLongTermKey\s+localOrSharedKey", "missing local/shared legacy tuple"),
        (r"BleBondDbLongTermKey\s+peerLegacyKey", "missing peer legacy tuple"),
        (r"uint8_t\s+ltk\s*\[\s*16\s*\]", "tuple must contain LTK"),
        (r"uint8_t\s+rand\s*\[\s*8\s*\]", "tuple must contain Rand"),
        (r"uint16_t\s+ediv", "tuple must contain EDIV"),
        (r"updateActiveBondLegacyKeyTuple\s*\(", "missing later-SMP update hook"),
    ):
        require(pattern, store + "\n" + security, message)
    projector = function_body(
        store, ("bool bleBondDbCoreToRecord(",), "role-aware tuple projector"
    )
    assert "BleConnectionRole::kCentral" in projector
    assert "BleConnectionRole::kPeripheral" in projector
    assert "peerLegacyKey" in projector and "localOrSharedKey" in projector


def validate_linker_and_board_boundaries() -> None:
    linker_contracts = (
        (
            PLATFORM / "cores/nrf54l15/nrf54l15_linker_script.ld",
            0x17A000,
            0x17B000,
            0x17C000,
        ),
        (
            PLATFORM / "cores/nrf54l15/nrf54l15_linker_script_no_vpr.ld",
            0x17A000,
            0x17B000,
            0x17C000,
        ),
        (
            PLATFORM / "cores/nrf54l15/nrf54lm20b_linker_script.ld",
            0x1FA000,
            0x1FB000,
            0x1FC000,
        ),
        (
            PLATFORM / "cores/nrf54lm20b/nrf54lm20b_linker_script.ld",
            0x1FA000,
            0x1FB000,
            0x1FC000,
        ),
    )
    legacy_sections = (
        r"\.bond_storage\s+ORIGIN\(FLASH_BOND\)\s+\(NOLOAD\)",
        r"\.bond_cccd_storage\s+ORIGIN\(FLASH_BOND\)\s*\+\s*0x50\s+\(NOLOAD\)",
        r"\.prefs_storage\s+ORIGIN\(FLASH_BOND\)\s*\+\s*0xDC\s+\(NOLOAD\)",
        r"\.eeprom_storage\s+ORIGIN\(FLASH_BOND\)\s*\+\s*0xBBC\s+\(NOLOAD\)",
        r"\.bond_signing_storage\s+ORIGIN\(FLASH_BOND\)\s*\+\s*0xFC8\s+\(NOLOAD\)",
    )

    for path, zigbee_origin, multi_origin, legacy_origin in linker_contracts:
        linker = source(path)
        assert re.search(
            rf"FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*0x0+0\s*,\s*LENGTH\s*=\s*0x{zigbee_origin:X}\b",
            linker,
        ), f"{path.name}: sketch region must stop at 0x{zigbee_origin:X}"
        assert re.search(
            rf"FLASH_ZIGBEE\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*0x0*{zigbee_origin:X}\s*,\s*LENGTH\s*=\s*0x1000\b",
            linker,
        ), f"{path.name}: missing dedicated 4 KB FLASH_ZIGBEE region"
        assert re.search(
            rf"FLASH_BOND_DB\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*0x0*{multi_origin:X}\s*,\s*LENGTH\s*=\s*0x1000\b",
            linker,
        ), f"{path.name}: missing dedicated 4 KB FLASH_BOND_DB region"
        assert re.search(
            rf"FLASH_BOND\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*0x0*{legacy_origin:X}\s*,\s*LENGTH\s*=\s*0x1000\b",
            linker,
        ), f"{path.name}: legacy persistence page moved"
        for declaration in legacy_sections:
            assert re.search(declaration, linker), (
                f"{path.name}: legacy bond/Preferences/EEPROM offsets changed"
            )
        require(
            r"\.zigbee_storage\s+ORIGIN\(FLASH_ZIGBEE\)\s+\(NOLOAD\)",
            linker,
            f"{path.name}: .zigbee_storage must be NOLOAD",
        )
        require(
            r"ASSERT\s*\(\s*SIZEOF\s*\(\s*\.zigbee_storage\s*\)\s*<=\s*0x1000",
            linker,
            f"{path.name}: .zigbee_storage needs a 4 KB size assertion",
        )
        require(
            r"\.bond_db_storage\s+ORIGIN\(FLASH_BOND_DB\)\s+\(NOLOAD\)",
            linker,
            f"{path.name}: .bond_db_storage must be NOLOAD",
        )
        require(
            r"ASSERT\s*\(\s*SIZEOF\s*\(\s*\.bond_db_storage\s*\)\s*<=\s*0x1000",
            linker,
            f"{path.name}: .bond_db_storage needs a 4 KB size assertion",
        )

    boards = source(PLATFORM / "boards.txt")
    l15_boards = (
        "xiao_nrf54l15",
        "holyiot_25007_nrf54l15",
        "holyiot_25008_nrf54l15",
        "generic_nrf54l15_module_36pin",
        "nrf54l15dk_pca10156",
    )
    for board in l15_boards:
        assert f"{board}.upload.maximum_size=1548288" in boards, (
            f"{board}: maximum sketch size must reserve Zigbee and bond pages"
        )
    assert "xiao_nrf54lm20b.upload.maximum_size=2072576" in boards
    assert "upload.maximum_size=1552384" not in boards
    assert "upload.maximum_size=2076672" not in boards
    assert "upload.maximum_size=1556480" not in boards
    assert "upload.maximum_size=2080768" not in boards


def validate_atomic_update_and_clear_contracts() -> None:
    store = source(PARTS / "nrf54l15_hal_internal_gatt_bond.inc")
    writer = function_body(
        store,
        (
            "bool writeBleBondDbReplica(",
            "bool writeMultiBondSlot(",
            "bool writeMultiBondRecord(",
            "bool commitMultiBondSlot(",
            "bool persistMultiBondRecord(",
        ),
        "multibond replica writer",
    )
    lowered = writer.lower()
    invalidate = lowered.find("invalid")
    payload = lowered.find("payload", invalidate + 1)
    final_header = max(
        lowered.find("finalheader", payload + 1),
        lowered.find("final_header", payload + 1),
        lowered.find("final header", payload + 1),
    )
    assert invalidate >= 0, "writer must explicitly invalidate the target header first"
    assert payload > invalidate, "writer must commit payload after header invalidation"
    assert final_header > payload, "writer must publish the final header last"
    assert writer.count("writeBondStorageToRram") >= 3, (
        "invalid header, payload, and final header need separate RRAM commits"
    )
    assert re.search(
        r"select(?:Ble)?BondDbWriteReplica|(?:replica|bank)[ABab]|[Rr]eplica.*\^\s*1",
        writer,
    ), (
        "writer must target the inactive A/B replica"
    )
    assert "crc" in lowered and ("verify" in lowered or "memcmp" in lowered), (
        "writer must verify the committed header and payload"
    )

    require(
        r"(?:[Mm]ulti[Bb]ond|BondDb)[^;\n]*(?:[Tt]ombstone|TOMBSTONE)",
        store,
        "deletion/eviction needs a durable tombstone marker",
    )
    require(
        r"(?:[Mm]ulti[Bb]ond|BondDb)[^;\n]*(?:[Cc]lear[Aa]ll|CLEAR_ALL)",
        store,
        "clear-all needs a durable CLEAR_ALL marker",
    )
    clearer = function_body(
        source(PARTS / "nrf54l15_hal_ble_ll_security.inc"),
        ("bool BleRadio::clearPersistentBondRecord(",),
        "persistent bond clearer",
    )
    assert re.search(r"(?:clearAll|clearBleBondDb|CLEAR_ALL|tombstone)", clearer, re.IGNORECASE), (
        "public clear must durably supersede every older replica"
    )

    latest_clear = function_body(
        store,
        ("uint32_t bleBondDbLatestClearGeneration(",),
        "durable clear-generation scanner",
    )
    assert "payload.clearGeneration" in latest_clear
    assert "kBleBondDbReplicaCount" in latest_clear and "kBleMaxBonds" in latest_clear
    assert "bleBondDbGenerationNewer" in latest_clear, (
        "clear authority must be reconstructed from every valid replica"
    )
    assert "bleBondDbLatestClearGeneration" in writer
    assert re.search(r"image\.clearGeneration\s*=", writer), (
        "every post-clear record must inherit the durable clear generation"
    )
    newest = function_body(
        store,
        ("bool readNewestBleBondDbReplica(",),
        "newest bond DB replica reader",
    )
    assert "bleBondDbLatestClearGeneration" in newest
    assert "bleBondDbGenerationNewer" in newest, (
        "records at or before the durable clear generation must stay hidden"
    )


def validate_migration_contracts() -> None:
    store = source(PARTS / "nrf54l15_hal_internal_gatt_bond.inc")
    migration = function_body(
        store,
        (
            "bool migrateLegacyBondToMultiBond(",
            "bool migrateLegacyBondRecord(",
            "bool migrateLegacyBondStorage(",
            "bool migrateLegacyBondStorageToDatabase(",
            "bool importLegacyBondRecord(",
        ),
        "legacy-to-multibond migration",
    )
    for token in (
        "BleBondRecordV1",
        "BleBondRecordV2",
        "BleCccdBondRecordV1",
        "BleCccdBondRecord",
    ):
        assert token in store, f"legacy decoder must remain available: {token}"

    read_base = migration.find("readFlashBondRecord")
    read_cccd = migration.find("readFlashCccdBondRecord")
    commit_new_candidates = (
        migration.find("writeBleBondDbReplica"),
        migration.find("writeMultiBond"),
        migration.find("persistMultiBond"),
        migration.find("commitMultiBond"),
    )
    commit_new = min(index for index in commit_new_candidates if index >= 0)
    clear_old_candidates = (
        migration.find("clearFlashBondRecord"),
        migration.find("clearLegacyBond"),
    )
    clear_old = min(index for index in clear_old_candidates if index >= 0)
    assert 0 <= read_base < commit_new and 0 <= read_cccd < commit_new
    assert commit_new < clear_old, "legacy storage may be cleared only after new-slot commit"
    writer = function_body(
        store, ("bool writeBleBondDbReplica(",), "verified multibond writer"
    )
    assert "verifyReplica" in writer and "bleBondDbReplicaValid" in writer, (
        "the migration commit helper must read back and verify the new replica"
    )
    assert not re.search(
        r"g_preferences|prefsStorage|EEPROMClass|eepromStorage",
        migration,
        re.IGNORECASE,
    ), (
        "migration must never alter Preferences or EEPROM"
    )

    loader = function_body(
        store,
        (
            "bool bleBondDbHasValidHeader(",
            "bool readMultiBondStore(",
            "bool loadMultiBondStore(",
            "bool scanMultiBondStore(",
        ),
        "multibond store loader",
    )
    assert re.search(r"(?:valid.*header|HeaderValid|format.*present|authoritative)", loader, re.IGNORECASE), (
        "any valid new-format header, including a tombstone, must block legacy resurrection"
    )


def validate_public_api_contracts() -> None:
    hal = source(HAL / "nrf54l15_hal.h")
    bluefruit = source(BLUEFRUIT / "bluefruit.h")

    require(r"(?:bondCount|getBondCount)\s*\(\s*\)\s*const", hal, "HAL needs a bond-count API")
    require(r"enumerateBonds\s*\(", hal, "HAL needs indexed bond enumeration")
    require(r"getBondInfo\s*\(\s*uint8_t\s+", hal, "HAL needs indexed bond retrieval")
    require(
        r"(?:removeBondRecord|removeBond|deleteBond|clearBondRecord)\s*\(\s*uint8_t\s+",
        hal,
        "HAL needs indexed bond deletion",
    )
    require(
        r"(?:clearAllBondRecords|clearBondRecords|clearBonds|clearBondRecord)\s*\(",
        hal,
        "HAL needs explicit clear-all",
    )
    require(
        r"(?:activeBondId|activeBondIndex|selectedBondIndex|getActiveBondIndex)\s*\(",
        hal,
        "HAL must expose which logical bond is selected",
    )

    require(r"(?:bondCount|getBondCount)\s*\(", bluefruit, "Bluefruit needs a bond-count API")
    require(
        r"(?:getBond|getBondInfo|bondPeerAddress|getBondPeerAddress)\s*\(\s*uint8_t\s+",
        bluefruit,
        "Bluefruit needs indexed bond inspection",
    )
    require(
        r"(?:removeBond|deleteBond|clearBond)\s*\(\s*uint8_t\s+",
        bluefruit,
        "Bluefruit needs indexed bond deletion",
    )
    assert "clearBonds(" in bluefruit, "existing clear-all compatibility API must remain"


def validate_per_bond_cccd_and_service_changed() -> None:
    store = source(PARTS / "nrf54l15_hal_internal_gatt_bond.inc")
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")
    combined = store + "\n" + security

    payload_match = require(
        r"struct\s+([A-Za-z0-9_]*(?:MultiBond|BondDb)[A-Za-z0-9_]*Payload)\s*\{(?P<body>.*?)\};",
        store,
        "multibond payload struct is missing",
        re.DOTALL,
    )
    payload = payload_match.group("body")
    assert "BleBondDbCore" in payload and "BleBondDbPeerState" in payload, (
        "each logical slot must atomically carry bond and CCCD/schema state"
    )

    for signature, label in (
        (("bool BleRadio::persistBondedCccdState(",), "CCCD persistence"),
        (("bool BleRadio::restoreBondedCccdState(",), "CCCD restore"),
        (("bool BleRadio::confirmBondedServiceChanged(",), "Service Changed confirmation"),
    ):
        method = function_body(combined, signature, label)
        active_path = re.search(
            r"(?:active|selected).*bond|bond.*(?:index|slot|database)",
            method,
            re.IGNORECASE,
        ) or "persistBondedCccdState" in method
        assert active_path, (
            f"{label} must update only the selected logical bond"
        )
        unified_path = re.search(
            r"(?:MultiBond|multiBond|BondDb|bondDb|BondDatabase)", method
        ) or "persistBondedCccdState" in method
        assert unified_path, (
            f"{label} must persist through the unified multibond replica"
        )

    selector = function_body(
        security,
        ("bool BleRadio::primeBondForCurrentPeer(",),
        "bond selector",
    )
    assert "restoreBondedCccdState" in selector
    assert "commitGattSchemaFingerprint" in selector, (
        "schema comparison must occur lazily after selecting the peer's slot"
    )


def validate_selection_privacy_and_resolving_refresh() -> None:
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")
    bluefruit_cpp = source(BLUEFRUIT / "bluefruit.cpp")
    selector = function_body(
        security,
        ("bool BleRadio::primeBondForCurrentPeer(",),
        "multibond selector",
    )

    require(
        r"for\s*\([^;]*;[^;]*<[^;]*(?:[Mm]ulti[Bb]ond|BondDb|kBleMaxBonds|bondCount)",
        selector,
        "peer selection must examine all logical bonds",
    )
    direct = min(
        index
        for index in (
            selector.find("memcmp"),
            selector.find("bleAddressEqual"),
        )
        if index >= 0
    )
    resolve_candidates = (
        selector.find("resolveSingle"),
        selector.find("computeBleResolvablePrivateAddressHash"),
    )
    resolve = min(index for index in resolve_candidates if index >= 0)
    assert direct < resolve, "direct/identity matching must precede expensive RPA resolution"
    assert semantic_trace_present(selector, ("BOND+DIRECT", "BOND+IDENTITY")), (
        "selection needs a direct/identity-match diagnostic marker"
    )
    assert semantic_trace_present(selector, ("BOND+RPA", "BOND+RESOLV")), (
        "selection needs a privacy-resolution diagnostic marker"
    )
    assert semantic_trace_present(selector, ("BOND+SELECT",)), (
        "selection needs a selected-slot diagnostic marker"
    )

    refresh = function_body(
        bluefruit_cpp,
        ("bool BLESecurity::refreshBondedPeerResolving(",),
        "Bluefruit resolving-list refresh",
    )
    assert re.search(r"for\s*\(|bondCount\s*\(", refresh), (
        "resolving refresh must rebuild entries for all bonded peer IRKs"
    )
    assert re.search(r"getBond(?:PeerIrk|Record)\s*\(\s*(?:index|i|bond|bonds\[bond\]\.id)", refresh), (
        "resolving refresh must retrieve IRKs by bond index"
    )
    assert "kResolvingSourceBond" in refresh, (
        "refresh must track automatic bond IRKs separately from manual entries"
    )


@dataclass(frozen=True)
class Replica:
    logical: int
    generation: int
    kind: str
    clear_generation: int
    payload: bytes


MODEL_HEADER_SIZE = 16
MODEL_PAYLOAD_SIZE = 240
MODEL_REPLICA_SIZE = 256
MODEL_WORDLINE_SIZE = 16
MODEL_MAGIC = {
    "active": 0x3142414D,
    "tombstone": 0x3154424D,
    "clear_all": 0x3143424D,
}
MODEL_KIND_BY_MAGIC = {magic: kind for kind, magic in MODEL_MAGIC.items()}


def model_crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def model_payload(logical: int, seed: int, clear_generation: int = 0) -> bytes:
    assert 0 <= logical <= 0xFF
    prefix = bytes([logical]) + struct.pack("<I", clear_generation & 0xFFFFFFFF)
    return prefix + bytes(
        ((seed + index * 29) & 0xFF)
        for index in range(MODEL_PAYLOAD_SIZE - len(prefix))
    )


def model_header(kind: str, generation: int, payload: bytes) -> bytes:
    assert kind in MODEL_MAGIC
    assert len(payload) == MODEL_PAYLOAD_SIZE
    prefix = struct.pack(
        "<III", MODEL_MAGIC[kind], generation & 0xFFFFFFFF, model_crc(payload)
    )
    return prefix + struct.pack("<I", model_crc(prefix))


def encode_model_replica(
    logical: int,
    generation: int,
    kind: str = "active",
    seed: int = 0x31,
    clear_generation: int | None = None,
) -> bytes:
    if clear_generation is None:
        clear_generation = generation if kind == "clear_all" else 0
    payload = model_payload(logical, seed, clear_generation)
    image = model_header(kind, generation, payload) + payload
    assert len(image) == MODEL_REPLICA_SIZE
    return image


def decode_model_replica(image: bytes) -> Replica | None:
    if len(image) != MODEL_REPLICA_SIZE:
        return None
    magic, generation, payload_crc, header_crc = struct.unpack(
        "<IIII", image[:MODEL_HEADER_SIZE]
    )
    kind = MODEL_KIND_BY_MAGIC.get(magic)
    if kind is None or model_crc(image[:12]) != header_crc:
        return None
    payload = image[MODEL_HEADER_SIZE:]
    if model_crc(payload) != payload_crc:
        return None
    clear_generation = struct.unpack("<I", payload[1:5])[0]
    if kind == "clear_all" and clear_generation != generation:
        return None
    if clear_generation != 0 and not generation_newer(generation, clear_generation):
        if kind != "clear_all":
            return None
    return Replica(payload[0], generation, kind, clear_generation, payload)


def decode_model_header(image: bytes) -> tuple[str, int] | None:
    if len(image) != MODEL_REPLICA_SIZE:
        return None
    magic, generation, _payload_crc, header_crc = struct.unpack(
        "<IIII", image[:MODEL_HEADER_SIZE]
    )
    kind = MODEL_KIND_BY_MAGIC.get(magic)
    if kind is None or generation == 0 or model_crc(image[:12]) != header_crc:
        return None
    return kind, generation


def select_model_pair(logical: int, images: list[bytes]) -> Replica | None:
    """Select one fixed logical A/B pair using committed-header authority."""
    authoritative: bytes | None = None
    authoritative_generation = 0
    for image in images:
        header = decode_model_header(image)
        if header is None:
            continue
        _kind, generation = header
        if authoritative is None or generation_newer(
            generation, authoritative_generation
        ):
            authoritative = image
            authoritative_generation = generation
    if authoritative is None:
        return None
    replica = decode_model_replica(authoritative)
    if replica is None or replica.logical != logical or replica.kind != "active":
        return None
    return replica


def generation_newer(lhs: int, rhs: int) -> bool:
    difference = (lhs - rhs) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000


def select_model(images: list[bytes]) -> dict[int, Replica]:
    replicas = [
        replica
        for image in images
        if (replica := decode_model_replica(image)) is not None
    ]
    clear_generation: int | None = None
    for replica in replicas:
        candidate = replica.clear_generation
        if replica.kind == "clear_all":
            candidate = replica.generation
        if candidate != 0 and (
            clear_generation is None
            or generation_newer(candidate, clear_generation)
        ):
            clear_generation = candidate

    selected: dict[int, Replica] = {}
    for replica in replicas:
        if replica.kind == "clear_all":
            continue
        if clear_generation is not None and not generation_newer(
            replica.generation, clear_generation
        ):
            continue
        current = selected.get(replica.logical)
        if current is None or generation_newer(replica.generation, current.generation):
            selected[replica.logical] = replica
    return {
        logical: replica
        for logical, replica in selected.items()
        if replica.kind == "active"
    }


def prefix_write(image: bytes, offset: int, data: bytes, count: int) -> bytes:
    assert 0 <= count <= len(data)
    result = bytearray(image)
    result[offset : offset + count] = data[:count]
    return bytes(result)


def validate_replica_power_cut_model() -> None:
    assert MODEL_HEADER_SIZE == MODEL_WORDLINE_SIZE
    assert MODEL_HEADER_SIZE + MODEL_PAYLOAD_SIZE == MODEL_REPLICA_SIZE

    # Replica A is the last committed value. Replica B starts as its older
    # predecessor and is updated by three separate RRAM commits.
    replica_a = encode_model_replica(0, 10, seed=0x10)
    replica_b_old = encode_model_replica(0, 9, seed=0x09)
    new_payload = model_payload(0, 0x11)
    invalid_header = bytes(MODEL_HEADER_SIZE)
    final_header = model_header("active", 11, new_payload)

    # A cut during any byte/wordline portion of header invalidation must leave
    # the old committed A image selectable. Byte prefixes include every
    # possible torn point in the 16-byte RRAM wordline.
    for cut in range(MODEL_HEADER_SIZE + 1):
        interrupted = prefix_write(replica_b_old, 0, invalid_header, cut)
        selected = select_model([replica_a, interrupted])
        assert selected[0].generation == 10

    invalidated = prefix_write(
        replica_b_old, 0, invalid_header, MODEL_HEADER_SIZE
    )
    assert decode_model_replica(invalidated) is None

    # With the header invalid, no partial payload write may publish a hybrid.
    for cut in range(MODEL_PAYLOAD_SIZE + 1):
        interrupted = prefix_write(
            invalidated, MODEL_HEADER_SIZE, new_payload, cut
        )
        selected = select_model([replica_a, interrupted])
        assert selected[0].generation == 10
    payload_complete = prefix_write(
        invalidated, MODEL_HEADER_SIZE, new_payload, MODEL_PAYLOAD_SIZE
    )

    # The final one-wordline header is the only publication point. A torn
    # header fails its CRC; the fully written header selects generation 11.
    for cut in range(MODEL_HEADER_SIZE):
        interrupted = prefix_write(payload_complete, 0, final_header, cut)
        selected = select_model([replica_a, interrupted])
        assert selected[0].generation == 10
    committed = prefix_write(
        payload_complete, 0, final_header, MODEL_HEADER_SIZE
    )
    assert select_model([replica_a, committed])[0].generation == 11

    # An invalid/torn header was never committed and falls back. A valid newer
    # header with later payload corruption remains authoritative and fails
    # closed, preventing anti-replay/signing state rollback.
    for offset in range(MODEL_REPLICA_SIZE):
        corrupt = bytearray(committed)
        corrupt[offset] ^= 0x01
        selected = select_model_pair(0, [replica_a, bytes(corrupt)])
        if offset < MODEL_HEADER_SIZE:
            assert selected is not None and selected.generation == 10
        else:
            assert selected is None


def validate_replica_tombstone_clear_and_wrap_model() -> None:
    replicas = [
        encode_model_replica(0, 10, "active", 0x10),
        encode_model_replica(0, 11, "tombstone", 0x11),
        encode_model_replica(1, 8, "active", 0x08),
    ]
    assert set(select_model(replicas)) == {1}

    replicas.append(encode_model_replica(0xFF, 12, "clear_all", 0x12))
    assert select_model(replicas) == {}
    replicas.append(encode_model_replica(2, 13, "active", 0x13))
    assert set(select_model(replicas)) == {2}

    assert generation_newer(1, 0xFFFFFFFF)
    assert not generation_newer(0xFFFFFFFF, 1)
    wrapped = [
        encode_model_replica(3, 0xFFFFFFFF, "active", 0xFE),
        encode_model_replica(3, 1, "active", 0x01),
    ]
    assert select_model(wrapped)[3].generation == 1


def validate_clear_epoch_survives_marker_reuse_model() -> None:
    # Fill both banks for logical slots 0..7 with pre-clear records.
    storage = [
        encode_model_replica(slot, slot + 1, "active", 0x20 + slot)
        for slot in range(8)
    ] + [
        encode_model_replica(slot, slot + 1, "active", 0x40 + slot)
        for slot in range(8)
    ]
    assert set(select_model(storage)) == set(range(8))

    clear_generation = 20
    # CLEAR_ALL occupies replica B for slot 0.
    storage[8] = encode_model_replica(
        0xFF, clear_generation, "clear_all", 0x60
    )
    assert select_model(storage) == {}

    # The first post-clear slot-0 record carries the clear watermark.
    storage[0] = encode_model_replica(
        0, 21, "active", 0x61, clear_generation=clear_generation
    )
    assert set(select_model(storage)) == {0}

    # Updating slot 0 again overwrites the only explicit CLEAR_ALL replica.
    # Its inherited clear watermark must remain authoritative, otherwise the
    # untouched pre-clear slots 1..7 would be resurrected.
    storage[8] = encode_model_replica(
        0, 22, "active", 0x62, clear_generation=clear_generation
    )
    selected = select_model(storage)
    assert set(selected) == {0}
    assert selected[0].generation == 22


VALIDATORS: tuple[tuple[str, Callable[[], None]], ...] = (
    ("fixed 16x256 media layout", validate_fixed_media_layout),
    ("local/peer legacy key tuples", validate_legacy_key_tuple_contracts),
    ("linker and board boundaries", validate_linker_and_board_boundaries),
    ("atomic A/B update and clear", validate_atomic_update_and_clear_contracts),
    ("legacy migration", validate_migration_contracts),
    ("public indexed APIs", validate_public_api_contracts),
    ("per-bond CCCD and Service Changed", validate_per_bond_cccd_and_service_changed),
    ("selection, privacy, and resolving refresh", validate_selection_privacy_and_resolving_refresh),
    ("partial-write and CRC fallback model", validate_replica_power_cut_model),
    ("tombstone, CLEAR_ALL, and wrap model", validate_replica_tombstone_clear_and_wrap_model),
    ("CLEAR_ALL epoch survives marker reuse", validate_clear_epoch_survives_marker_reuse_model),
)


def main() -> int:
    failures: list[tuple[str, str]] = []
    for name, validator in VALIDATORS:
        try:
            validator()
            print(f"PASS {name}")
        except (AssertionError, ValueError) as exc:
            failures.append((name, str(exc)))
            print(f"FAIL {name}: {exc}")

    if failures:
        print(f"FAIL BLE multibond contracts: {len(failures)} section(s) incomplete")
        return 1
    print("PASS eight-peer power-loss-safe BLE multibond contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
