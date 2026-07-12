#!/usr/bin/env python3
"""Host models and source contracts for complete BLE security policy support.

The pure models below are executable protocol requirements.  The source checks
are intentionally forward-looking: they fail until the corresponding Bluefruit
and clean-controller contracts have been implemented.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, IntEnum
from pathlib import Path
import re
from typing import Callable, Iterable


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


def require_any(patterns: Iterable[str], text: str, message: str) -> None:
    assert any(re.search(pattern, text, re.DOTALL) for pattern in patterns), message


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


class IoCapability(IntEnum):
    DISPLAY_ONLY = 0
    DISPLAY_YES_NO = 1
    KEYBOARD_ONLY = 2
    NO_INPUT_NO_OUTPUT = 3
    KEYBOARD_DISPLAY = 4


class GapRole(Enum):
    CENTRAL = "central"
    PERIPHERAL = "peripheral"


class UserAction(Enum):
    NONE = "N"
    DISPLAY = "D"
    INPUT = "I"
    NUMERIC_COMPARISON = "C"
    ROLE_DEPENDENT = "R"


# Bluetooth Core, Vol 3, Part H, 2.3.5.1, indexed [remote][local].
_LEGACY_ACTIONS = (
    "NNINI",
    "NNINI",
    "DDIND",
    "NNNNN",
    "DDINR",
)
_SC_ACTIONS = (
    "NNINI",
    "NCINC",
    "DDIND",
    "NNNNN",
    "DCINC",
)


def association_action(
    *,
    secure_connections: bool,
    role: GapRole,
    local_io: IoCapability,
    remote_io: IoCapability,
    mitm_requested: bool,
) -> UserAction:
    if not mitm_requested:
        return UserAction.NONE
    rows = _SC_ACTIONS if secure_connections else _LEGACY_ACTIONS
    action = UserAction(rows[int(remote_io)][int(local_io)])
    if action is UserAction.ROLE_DEPENDENT:
        return (
            UserAction.DISPLAY
            if role is GapRole.CENTRAL
            else UserAction.INPUT
        )
    return action


@dataclass(frozen=True)
class SecurityPolicy:
    bonding_enabled: bool = True
    mitm_required: bool = False
    secure_connections_enabled: bool = True
    secure_connections_required: bool = False
    min_encryption_key_size: int = 7
    max_encryption_key_size: int = 16

    def __post_init__(self) -> None:
        if self.secure_connections_required and not self.secure_connections_enabled:
            raise ValueError("Secure Connections cannot be required while disabled")
        if (
            self.min_encryption_key_size < 7
            or self.max_encryption_key_size > 16
            or self.min_encryption_key_size > self.max_encryption_key_size
        ):
            raise ValueError("encryption key size range must be within 7..16")


@dataclass(frozen=True)
class BondSecurity:
    authenticated: bool
    secure_connections: bool


def bond_meets_policy(bond: BondSecurity, policy: SecurityPolicy) -> bool:
    if policy.mitm_required and not bond.authenticated:
        return False
    if policy.secure_connections_required and not bond.secure_connections:
        return False
    return True


def parse_six_digit_passkey(passkey: bytes | bytearray) -> int:
    if len(passkey) != 6 or any(value < ord("0") or value > ord("9") for value in passkey):
        raise ValueError("passkey must contain exactly six ASCII digits")
    value = 0
    for digit in passkey:
        value = (value * 10) + digit - ord("0")
    return value


def format_six_digit_passkey(passkey: int) -> bytes:
    if passkey < 0 or passkey > 999_999:
        raise ValueError("numeric passkey is outside the six-digit range")
    return f"{passkey:06d}".encode("ascii")


class PasskeyRequestModel:
    """Reference request lifecycle with stale-reply protection."""

    def __init__(self) -> None:
        self._last_id = 0
        self.pending_id: int | None = None
        self.callback_dispatched = False
        self.accepted_passkey: int | None = None

    def begin(self) -> int:
        self._last_id = (self._last_id + 1) & 0xFFFF_FFFF
        if self._last_id == 0:
            self._last_id = 1
        self.pending_id = self._last_id
        self.callback_dispatched = False
        self.accepted_passkey = None
        return self._last_id

    def take_callback_notification(self) -> int | None:
        if self.pending_id is None or self.callback_dispatched:
            return None
        self.callback_dispatched = True
        return self.pending_id

    def reply(self, request_id: int, passkey: bytes | bytearray) -> bool:
        if self.pending_id is None or request_id != self.pending_id:
            return False
        try:
            parsed = parse_six_digit_passkey(passkey)
        except ValueError:
            return False
        self.accepted_passkey = parsed
        self.pending_id = None
        return True

    def disconnect_or_timeout(self) -> None:
        self.pending_id = None
        self.callback_dispatched = False
        self.accepted_passkey = None


class Phase3Pdu(Enum):
    ENCRYPTION_INFORMATION = "Encryption Information"
    MASTER_IDENTIFICATION = "Master Identification"
    IDENTITY_INFORMATION = "Identity Information"
    IDENTITY_ADDRESS = "Identity Address Information"
    SIGNING_INFORMATION = "Signing Information"


class Phase3Model:
    """SMP phase-3 ordering and one-PDU-per-ACK reference model."""

    def __init__(
        self,
        *,
        role: GapRole,
        secure_connections: bool,
        bonding_enabled: bool = True,
        distribute_enc: bool = True,
        distribute_id: bool = True,
        distribute_sign: bool = True,
    ) -> None:
        self.role = role
        self.bonding_enabled = bonding_enabled
        self.peer_distribution_complete = False
        self.outstanding: Phase3Pdu | None = None
        self.sent: list[Phase3Pdu] = []
        self.acked: list[Phase3Pdu] = []
        self._next_index = 0
        self.sequence: list[Phase3Pdu] = []
        if bonding_enabled:
            if distribute_enc and not secure_connections:
                self.sequence.extend(
                    (
                        Phase3Pdu.ENCRYPTION_INFORMATION,
                        Phase3Pdu.MASTER_IDENTIFICATION,
                    )
                )
            if distribute_id:
                self.sequence.extend(
                    (
                        Phase3Pdu.IDENTITY_INFORMATION,
                        Phase3Pdu.IDENTITY_ADDRESS,
                    )
                )
            if distribute_sign:
                self.sequence.append(Phase3Pdu.SIGNING_INFORMATION)

    def set_peer_distribution_complete(self) -> None:
        self.peer_distribution_complete = True

    def poll(self) -> Phase3Pdu | None:
        if not self.bonding_enabled or self.outstanding is not None:
            return None
        if self.role is GapRole.CENTRAL and not self.peer_distribution_complete:
            return None
        if self._next_index == len(self.sequence):
            return None
        self.outstanding = self.sequence[self._next_index]
        self.sent.append(self.outstanding)
        return self.outstanding

    def acknowledge(self, pdu: Phase3Pdu) -> bool:
        if pdu is not self.outstanding:
            return False
        self.acked.append(pdu)
        self.outstanding = None
        self._next_index += 1
        return True

    @property
    def complete(self) -> bool:
        if not self.bonding_enabled:
            return True
        return (
            self.peer_distribution_complete
            and self.outstanding is None
            and self._next_index == len(self.sequence)
        )


def reduce_legacy_ltk(key: bytes, key_size: int) -> bytes:
    if len(key) != 16:
        raise ValueError("LTK must be 16 bytes")
    if key_size < 7 or key_size > 16:
        raise ValueError("negotiated encryption key size must be 7..16")
    return key[:key_size] + bytes(16 - key_size)


def validate_policy_model() -> None:
    default = SecurityPolicy()
    assert default == SecurityPolicy(True, False, True, False, 7, 16)
    legacy_just_works = BondSecurity(authenticated=False, secure_connections=False)
    legacy_authenticated = BondSecurity(authenticated=True, secure_connections=False)
    sc_just_works = BondSecurity(authenticated=False, secure_connections=True)
    sc_authenticated = BondSecurity(authenticated=True, secure_connections=True)

    assert bond_meets_policy(legacy_just_works, default)
    assert bond_meets_policy(legacy_authenticated, SecurityPolicy(mitm_required=True))
    assert not bond_meets_policy(legacy_just_works, SecurityPolicy(mitm_required=True))
    assert bond_meets_policy(sc_just_works, SecurityPolicy(secure_connections_required=True))
    assert not bond_meets_policy(legacy_authenticated, SecurityPolicy(secure_connections_required=True))
    assert bond_meets_policy(
        sc_authenticated,
        SecurityPolicy(mitm_required=True, secure_connections_required=True),
    )
    assert bond_meets_policy(
        legacy_authenticated,
        SecurityPolicy(bonding_enabled=False, mitm_required=True),
    ), "bonding controls new storage, not the strength of an existing key"
    try:
        SecurityPolicy(secure_connections_enabled=False, secure_connections_required=True)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid SC enabled/required combination was accepted")
    for minimum, maximum in ((0, 16), (6, 16), (7, 17), (12, 11)):
        try:
            SecurityPolicy(
                min_encryption_key_size=minimum,
                max_encryption_key_size=maximum,
            )
        except ValueError:
            continue
        raise AssertionError(f"invalid encryption key-size range accepted: {minimum}..{maximum}")


def validate_passkey_and_request_model() -> None:
    assert parse_six_digit_passkey(b"000042") == 42
    assert format_six_digit_passkey(42) == b"000042"
    assert parse_six_digit_passkey(b"999999") == 999_999
    for invalid in (b"", b"12345", b"1234567", b"12345\0", b"12 456", b"abcdef"):
        try:
            parse_six_digit_passkey(invalid)
        except ValueError:
            continue
        raise AssertionError(f"invalid passkey accepted: {invalid!r}")

    requests = PasskeyRequestModel()
    first = requests.begin()
    assert requests.take_callback_notification() == first
    assert requests.take_callback_notification() is None, "callback dispatched twice"
    second = requests.begin()
    assert second != first
    assert not requests.reply(first, b"000042"), "stale request ID accepted"
    assert not requests.reply(second, b"12x456"), "non-digit reply accepted"
    assert requests.reply(second, b"000042")
    assert requests.accepted_passkey == 42
    assert not requests.reply(second, b"000043"), "completed request accepted twice"

    third = requests.begin()
    requests.disconnect_or_timeout()
    assert not requests.reply(third, b"000043"), "reply crossed disconnect/timeout"
    requests._last_id = 0xFFFF_FFFF
    assert requests.begin() == 1, "request ID zero must be skipped after wrap"


def _matrix(role: GapRole, secure_connections: bool) -> tuple[str, ...]:
    return tuple(
        "".join(
            association_action(
                secure_connections=secure_connections,
                role=role,
                local_io=local,
                remote_io=remote,
                mitm_requested=True,
            ).value
            for local in IoCapability
        )
        for remote in IoCapability
    )


def validate_association_matrices() -> None:
    expected_sc = ("NNINI", "NCINC", "DDIND", "NNNNN", "DCINC")
    expected_legacy_central = ("NNINI", "NNINI", "DDIND", "NNNNN", "DDIND")
    expected_legacy_peripheral = ("NNINI", "NNINI", "DDIND", "NNNNN", "DDINI")
    assert _matrix(GapRole.CENTRAL, True) == expected_sc
    assert _matrix(GapRole.PERIPHERAL, True) == expected_sc
    assert _matrix(GapRole.CENTRAL, False) == expected_legacy_central
    assert _matrix(GapRole.PERIPHERAL, False) == expected_legacy_peripheral
    for role in GapRole:
        for secure_connections in (False, True):
            for local in IoCapability:
                for remote in IoCapability:
                    assert association_action(
                        secure_connections=secure_connections,
                        role=role,
                        local_io=local,
                        remote_io=remote,
                        mitm_requested=False,
                    ) is UserAction.NONE


def validate_phase3_model() -> None:
    expected_legacy = [
        Phase3Pdu.ENCRYPTION_INFORMATION,
        Phase3Pdu.MASTER_IDENTIFICATION,
        Phase3Pdu.IDENTITY_INFORMATION,
        Phase3Pdu.IDENTITY_ADDRESS,
        Phase3Pdu.SIGNING_INFORMATION,
    ]
    peripheral = Phase3Model(role=GapRole.PERIPHERAL, secure_connections=False)
    for pdu in expected_legacy:
        assert peripheral.poll() is pdu
        assert peripheral.poll() is None, "next PDU sent before link-layer ACK"
        wrong = (
            Phase3Pdu.SIGNING_INFORMATION
            if pdu is not Phase3Pdu.SIGNING_INFORMATION
            else Phase3Pdu.IDENTITY_INFORMATION
        )
        assert not peripheral.acknowledge(wrong), "wrong PDU ACK advanced phase 3"
        assert peripheral.acknowledge(pdu)
        assert not peripheral.acknowledge(pdu), "duplicate ACK advanced phase 3"
    assert peripheral.sent == expected_legacy
    assert not peripheral.complete, "peer keys are still outstanding"
    peripheral.set_peer_distribution_complete()
    assert peripheral.complete

    central = Phase3Model(role=GapRole.CENTRAL, secure_connections=False)
    assert central.poll() is None, "Central distributed before Peripheral completed"
    central.set_peer_distribution_complete()
    assert central.poll() is Phase3Pdu.ENCRYPTION_INFORMATION

    secure_connections = Phase3Model(
        role=GapRole.PERIPHERAL,
        secure_connections=True,
    )
    assert Phase3Pdu.ENCRYPTION_INFORMATION not in secure_connections.sequence
    assert Phase3Pdu.MASTER_IDENTIFICATION not in secure_connections.sequence
    assert secure_connections.sequence == expected_legacy[2:]

    non_bonding = Phase3Model(
        role=GapRole.PERIPHERAL,
        secure_connections=False,
        bonding_enabled=False,
    )
    assert non_bonding.poll() is None and non_bonding.complete


def validate_key_size_model() -> None:
    key = bytes(range(1, 17))
    assert reduce_legacy_ltk(key, 16) == key
    assert reduce_legacy_ltk(key, 7) == key[:7] + bytes(9)
    assert reduce_legacy_ltk(key, 12) == key[:12] + bytes(4)
    for bad_size in (0, 6, 17, 255):
        try:
            reduce_legacy_ltk(key, bad_size)
        except ValueError:
            continue
        raise AssertionError(f"invalid key size accepted: {bad_size}")


def validate_bluefruit_policy_source() -> None:
    header = source(BLUEFRUIT / "bluefruit.h")
    implementation = source(BLUEFRUIT / "bluefruit.cpp")

    require(r"void\s+setMITM\s*\(\s*bool\s+\w+\s*\)\s*;", header,
            "missing upstream-compatible BLESecurity::setMITM(bool)")
    require(r"bool\s+setPIN\s*\(\s*const\s+char\s*\*\s*\w+\s*\)\s*;", header,
            "BLESecurity::setPIN must return validation success")
    for pattern, message in (
        (r"(?:bool|void)\s+setBondingEnabled\s*\(\s*bool", "missing bonding control"),
        (r"(?:bool|void)\s+setSecureConnectionsEnabled\s*\(\s*bool", "missing SC enable control"),
        (r"(?:bool|void)\s+setSecureConnectionsRequired\s*\(\s*bool", "missing SC require control"),
        (r"bool\s+setEncryptionKeySize\s*\(\s*uint8_t[^,]*,\s*uint8_t", "missing encryption key-size range control"),
    ):
        require(pattern, header, message)

    io_caps = function_body(
        implementation,
        ("void BLESecurity::setIOCaps(uint8_t", "void BLESecurity::setIOCaps(bool"),
        "BLESecurity::setIOCaps",
    )
    assert "setMITM" not in io_caps and "mitm_required_" not in io_caps, (
        "IO capability must not silently infer MITM policy"
    )
    set_pin = function_body(
        implementation,
        ("bool BLESecurity::setPIN(",),
        "BLESecurity::setPIN",
    )
    assert "setMITM(true)" in set_pin, "setPIN compatibility must explicitly enable MITM"
    assert "setSecureConnectionsEnabled(false)" not in set_pin
    assert "secure_connections_enabled_ = false" not in set_pin, (
        "setPIN must not silently downgrade Secure Connections"
    )
    for callback in (
        "bool BLESecurity::setPairPasskeyCallback(",
        "void BLESecurity::setPairPasskeyRequestCallback(",
    ):
        assert "setMITM(true)" in function_body(
            implementation, (callback,), callback
        ), f"{callback} must preserve upstream MITM compatibility"


def validate_hal_policy_and_bond_source() -> None:
    header = source(HAL / "nrf54l15_hal.h")
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")
    att = source(PARTS / "nrf54l15_hal_ble_att_l2cap.inc")
    rx = source(PARTS / "nrf54l15_hal_ble_peripheral_event_rx.inc")

    for field, default in (
        ("bondingEnabled", "true"),
        ("mitmRequired", "false"),
        ("secureConnectionsEnabled", "true"),
        ("secureConnectionsRequired", "false"),
        ("minEncryptionKeySize", "7U"),
        ("maxEncryptionKeySize", "16U"),
    ):
        require(
            rf"(?:bool|uint8_t)\s+{field}\s*(?:=\s*{default})?\s*;",
            header,
            f"BleSecurityPolicy is missing {field}",
        )
    assert "BleSecurityPolicy" in header
    assert "setSecurityPolicy" in header
    require_any(
        (
            r"BleSecurityPolicy\s+securityPolicy_",
            r"BleSecurityPolicy\s+configuredSecurityPolicy_",
        ),
        header,
        "missing configured security policy",
    )
    require_any(
        (
            r"BleSecurityPolicy\s+smpSecurityPolicy_",
            r"BleSecurityPolicy\s+activeSecurityPolicy_",
        ),
        header,
        "pairing must snapshot policy independently of live configuration",
    )
    require(r"bondMeetsSecurityPolicy\s*\(", header + security,
            "missing persisted-bond strength predicate")
    eligibility = function_body(
        security,
        ("bool BleRadio::bondMeetsSecurityPolicy(", "bool bondMeetsSecurityPolicy("),
        "bond policy predicate",
    )
    for token in ("mitmRequired", "secureConnectionsRequired", "authenticated"):
        assert token in eligibility, f"bond predicate does not check {token}"
    require_any(
        (r"secureConnections", r"SecureConnections", r"kBleBondFlagSecureConnections"),
        eligibility,
        "bond predicate does not check the bond's SC strength",
    )

    request_pairing = function_body(
        security,
        ("bool BleRadio::requestPairing()",),
        "BleRadio::requestPairing",
    )
    assert "bondMeetsSecurityPolicy" in request_pairing
    assert "smpLocalIoCapabilities_ != kSmpIoCapNoInputNoOutput" not in request_pairing
    require_any(
        (r"buildLocalPairingAuthReq\s*\(", r"securityAuthReqForPolicy\s*\("),
        security + att,
        "Pairing Request/Response AuthReq must be built from explicit policy",
    )
    require_any(
        (r"bondEligibleForCurrentSecurityPolicy_", r"activeBondMeetsSecurityPolicy_"),
        header + security + rx,
        "matching a peer identity must be separate from bond-policy eligibility",
    )


def validate_passkey_request_source() -> None:
    bluefruit_header = source(BLUEFRUIT / "bluefruit.h")
    bluefruit_source = source(BLUEFRUIT / "bluefruit.cpp")
    header = source(HAL / "nrf54l15_hal.h")
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")

    require(
        r"typedef\s+void\s*\(\s*\*\s*pair_passkey_(?:req|request)_(?:cb|callback)_t\s*\)"
        r"\s*\(\s*uint16_t[^,]*,\s*uint8_t\s+\w+\s*\[\s*6\s*\]\s*\)",
        bluefruit_header,
        "missing upstream-compatible six-byte passkey request callback",
        re.DOTALL,
    )
    assert "setPairPasskeyRequestCallback" in bluefruit_header
    assert "getPendingPairingPasskeyRequest" in bluefruit_header
    require(
        r"replyPendingPairingPasskey\s*\(\s*uint32_t\s+\w+\s*,\s*"
        r"(?:const\s+)?uint8_t[^)]*\[\s*6\s*\]",
        bluefruit_header,
        "missing request-ID passkey input reply overload",
        re.DOTALL,
    )
    manager_dispatch = function_body(
        bluefruit_source,
        ("void maybeDispatchSecurityCallbacks()",),
        "foreground security callback dispatcher",
    )
    for token in (
        "getPendingPairingPasskeyRequest",
        "pair_passkey_request_callback_",
        "replyPendingPairingPasskey",
        "security_passkey_request_callback_id_",
        "memset(requestedPasskey, 0xFF",
        "isValidSixDigitPasskey(requestedPasskey)",
    ):
        assert token in manager_dispatch, f"passkey input dispatcher missing {token}"
    assert "encrypted && !security_last_encrypted_ && !pairingActive" in manager_dispatch, (
        "secured callback must not expose the link before SMP key distribution completes"
    )
    connection_edge = function_body(
        bluefruit_source,
        ("void handleConnectionEdge(bool",),
        "connection-edge security setup",
    )
    assert "radio_.hasBondRecord()" in connection_edge
    assert "radio_.requestPairing()" in connection_edge, (
        "a bonded central reconnect must proactively start stored-key encryption"
    )

    assert "getPendingPairingPasskeyRequest" in header
    require(
        r"replyPendingPairingPasskey\s*\(\s*uint32_t\s+\w+\s*,\s*"
        r"const\s+uint8_t[^)]*\[\s*6\s*\]",
        header,
        "HAL must expose request-ID passkey reply",
        re.DOTALL,
    )
    for state in (
        "PasskeyInput",
        "smpPendingUserPasskeyRequestId_",
        "smpPairingPasskeyValue_",
        "smpPairingPasskeyValid_",
    ):
        assert state in header, f"missing passkey-input state: {state}"
    reply = function_body(
        security,
        ("bool BleRadio::replyPendingPairingPasskey(uint32_t",),
        "request-ID passkey reply",
    )
    for token in (
        "bleEnterCritical",
        "connected_",
        "smpPendingUserPasskeyRequestId_",
        "smpPairingState_",
        "smpPairingPasskeyValid_",
    ):
        assert token in reply, f"passkey reply does not revalidate/update {token}"
    assert "restartSmpTimer" not in reply, "user input must not restart the 30-second SMP timer"
    clear = function_body(
        security,
        ("void BleRadio::clearSmpPairingState()",),
        "SMP state reset",
    )
    for token in (
        "smpPendingUserPasskeyRequestId_",
        "smpPairingPasskeyValid_",
        "PasskeyInput",
    ):
        assert token in clear, f"disconnect/timeout reset does not clear {token}"


def validate_sc_and_legacy_defer_resume_source() -> None:
    header = source(HAL / "nrf54l15_hal.h")
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")
    att = source(PARTS / "nrf54l15_hal_ble_att_l2cap.inc")

    preparation_result = require(
        r"enum\s+class\s+\w*Passkey\w*\s*(?::\s*\w+\s*)?\{([^}]*)\}",
        header,
        "missing tri-state SC passkey preparation result",
        re.DOTALL,
    ).group(1)
    for result in ("Ready", "Pending", "Fatal"):
        assert result in preparation_result, (
            f"SC passkey preparation needs a distinct {result} result"
        )
    prepare = function_body(
        security,
        ("BleSmpPasskeyPreparationResult BleRadio::prepareSecureConnectionsPasskey(",),
        "tri-state SC passkey preparation",
    )
    assert "PasskeyInput" in prepare and "Pending" in prepare
    assert "restartSmpTimer" not in prepare, "passkey wait must keep the original SMP deadline"
    service = function_body(
        security,
        ("void BleRadio::serviceSecureConnectionsWork()",),
        "deferred Secure Connections work",
    )
    assert "Pending" in service
    assert "smpSecureConnectionsDeferredConfirm_" in service

    for contract in (
        "resolveLegacyPairingIoAction",
        "smpLegacyConfirmDeferred_",
        "serviceDeferredPairingConfirm",
    ):
        assert contract in header + security + att, f"missing legacy passkey transition: {contract}"
    legacy_resolver = function_body(
        security,
        (
            "uint8_t BleRadio::resolveLegacyPairingIoAction(",
            "uint8_t resolveLegacyPairingIoAction(",
        ),
        "legacy association-model resolver",
    )
    assert "kSmpScInitIoActions" not in legacy_resolver
    tk = function_body(
        security,
        ("void BleRadio::buildLegacyTemporaryKey(",),
        "legacy temporary key builder",
    )
    assert "smpPairingPasskeyValid_" in tk and "smpPairingPasskeyValue_" in tk
    authenticated = function_body(
        security,
        ("bool BleRadio::currentPairingIsAuthenticated()",),
        "pairing authentication classifier",
    )
    assert "smpPairingPasskeyValid_" in authenticated, (
        "dynamic legacy passkey pairing must be marked authenticated"
    )
    assert "smpLegacyConfirmDeferred_" in att, (
        "Pairing Confirm must be buffered while legacy input is pending"
    )


def validate_local_enckey_distribution_source() -> None:
    header = source(HAL / "nrf54l15_hal.h")
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")
    att = source(PARTS / "nrf54l15_hal_ble_att_l2cap.inc")

    for state in (
        "smpLocalLtk_",
        "smpLocalRand_",
        "smpLocalEdiv_",
        "smpDistributeLocalEncKey_",
        "smpLocalEncInfoSent_",
        "smpLocalEncInfoAcked_",
        "smpLocalMasterIdSent_",
        "smpLocalMasterIdAcked_",
    ):
        assert state in header, f"missing local legacy EncKey state: {state}"
    prepare = function_body(
        security,
        (
            "bool BleRadio::prepareLocalLegacyEncryptionKey(",
            "bool BleRadio::prepareLocalLegacyKey(",
        ),
        "local legacy LTK/Rand/EDIV generator",
    )
    for token in (
        "fillBleSecurityRandomBytes",
        "smpLocalLtk_",
        "smpLocalRand_",
        "smpLocalEdiv_",
        "reduceSmpEncryptionKey",
    ):
        assert token in prepare, f"local legacy key generator missing {token}"

    distribution = function_body(
        security,
        ("void BleRadio::serviceSmpKeyDistribution()",),
        "SMP phase-3 distributor",
    )
    ordered_tokens = (
        "kSmpCodeEncryptionInformation",
        "kSmpCodeMasterIdentification",
        "kSmpCodeIdentityInformation",
        "kSmpCodeIdentityAddressInformation",
        "kSmpCodeSigningInformation",
    )
    positions = [distribution.find(token) for token in ordered_tokens]
    assert all(position >= 0 for position in positions), "phase 3 does not emit every negotiated key PDU"
    assert positions == sorted(positions), "phase-3 PDU order is not EncInfo, MasterId, IdInfo, IdAddr, SignInfo"
    assert "smpLocalEncInfoAcked_" in distribution
    assert "smpLocalMasterIdAcked_" in distribution
    assert "smpLocalInitiator_" in distribution, (
        "Central must wait for the Peripheral's complete distribution"
    )

    ack = function_body(
        security,
        ("void BleRadio::noteLastSmpTxAcknowledged()",),
        "SMP TX acknowledgement transition",
    )
    for token in (
        "kSmpCodeEncryptionInformation",
        "smpLocalEncInfoAcked_",
        "kSmpCodeMasterIdentification",
        "smpLocalMasterIdAcked_",
    ):
        assert token in ack, f"legacy EncKey ACK state missing {token}"
    complete = function_body(
        security,
        ("bool BleRadio::completeSmpKeyDistributionIfDone()",),
        "SMP phase-3 completion",
    )
    for token in ("smpLocalEncInfoAcked_", "smpLocalMasterIdAcked_", "peerEncDone"):
        assert token in complete, f"phase-3 completion does not wait for {token}"
    require_any(
        (
            r"smpSecureConnectionsActive_[^;\n]*[~&][^;\n]*kSmpKeyDistributionEncMask",
            r"if\s*\(\s*smpSecureConnectionsActive_\s*\)[^{]*\{[^}]*EncKey",
        ),
        att + security,
        "Secure Connections negotiation must strip legacy EncKey distribution",
    )


def run_group(name: str, check: Callable[[], None]) -> str | None:
    try:
        check()
    except Exception as error:  # Keep every contract group visible in one run.
        print(f"FAIL {name}: {error}")
        return f"{name}: {error}"
    print(f"PASS {name}")
    return None


def main() -> int:
    groups: tuple[tuple[str, Callable[[], None]], ...] = (
        ("model.policy_eligibility", validate_policy_model),
        ("model.passkey_request_ids", validate_passkey_and_request_model),
        ("model.association_5x5", validate_association_matrices),
        ("model.phase3_ack_order", validate_phase3_model),
        ("model.legacy_key_size", validate_key_size_model),
        ("source.bluefruit_policy", validate_bluefruit_policy_source),
        ("source.hal_policy_and_bonds", validate_hal_policy_and_bond_source),
        ("source.passkey_request", validate_passkey_request_source),
        ("source.sc_legacy_defer_resume", validate_sc_and_legacy_defer_resume_source),
        ("source.local_enckey_distribution", validate_local_enckey_distribution_source),
    )
    failures = [failure for name, check in groups if (failure := run_group(name, check))]
    if failures:
        print(f"FAILED {len(failures)}/{len(groups)} BLE security contract groups")
        return 1
    print(f"PASS all {len(groups)} BLE security contract groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
