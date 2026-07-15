#!/usr/bin/env python3
"""Host-only parser tests for the two-board CHIP Inet runner."""

from __future__ import annotations

import contextlib
import io
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import test_matter_inet_transport as runner  # noqa: E402


def passing_pair() -> tuple[runner.InetResults, runner.InetResults]:
    leader = runner.InetResults(
        "/dev/leader",
        role="leader",
        rloc16="4800",
        partition_id="1234ABCD",
        begin_ok=True,
        role_mode="leader",
        wipe=True,
        dataset_ok=True,
        dataset_match=True,
        dataset_hex=runner.EXPECTED_DATASET_HEX,
        radio_snapshot_seen=True,
        radio_enabled=True,
        radio_ready=True,
        radio_channel=15,
    )
    peer = runner.InetResults(
        "/dev/peer",
        role="child",
        rloc16="4801",
        partition_id="1234ABCD",
        begin_ok=True,
        role_mode="child",
        wipe=True,
        dataset_ok=True,
        dataset_match=True,
        dataset_hex=runner.EXPECTED_DATASET_HEX,
        radio_snapshot_seen=True,
        radio_enabled=True,
        radio_ready=True,
        radio_channel=15,
        discovery_passed=True,
        done=True,
        reported_passes=len(runner.PAYLOAD_SIZES),
        reported_failures=0,
    )
    peer.passes.update(runner.PAYLOAD_SIZES)
    peer.pass_modes.update(runner.EXPECTED_TRANSPORT_MODES)
    return leader, peer


class MatterInetRunnerTest(unittest.TestCase):
    @staticmethod
    def validate(first: runner.InetResults, second: runner.InetResults) -> bool:
        with contextlib.redirect_stdout(io.StringIO()):
            return runner.validate(first, second)

    def test_parser_records_status_and_results(self) -> None:
        result = runner.InetResults("/dev/test")
        runner.parse_line(
            "inet_status role=router rloc16=0x4801 part=0x1234ABCD "
            "role_mode=child wipe=1 dataset_ok=1 dataset_match=1 "
            f"dataset_hex={runner.EXPECTED_DATASET_HEX}",
            result,
        )
        runner.parse_line("inet_boot dataset_ok=1 begin_ok=1", result)
        runner.parse_line("inet_discovery_pass mode=multicast", result)
        runner.parse_line("inet_pass len=512 mode=unicast", result)
        runner.parse_line("inet_done pass=5 fail=0", result)
        self.assertEqual(result.role, "router")
        self.assertEqual(result.rloc16, "4801")
        self.assertEqual(result.partition_id, "1234ABCD")
        self.assertTrue(result.begin_ok)
        self.assertEqual(result.role_mode, "child")
        self.assertTrue(result.wipe)
        self.assertTrue(result.dataset_ok)
        self.assertTrue(result.dataset_match)
        self.assertEqual(result.dataset_hex, runner.EXPECTED_DATASET_HEX)
        self.assertTrue(result.discovery_passed)
        self.assertIn(512, result.passes)
        self.assertEqual(result.pass_modes[512], "unicast")
        self.assertTrue(result.done)
        self.assertEqual(result.reported_passes, 5)
        self.assertEqual(result.reported_failures, 0)

    def test_parser_records_radio_snapshot(self) -> None:
        result = runner.InetResults("/dev/test")
        runner.parse_line(
            "inet_radio snapshot=1 state=2 enabled=1 ready=1 ch=15 "
            "tx_req=12 tx_done=11 tx_err=0 tx_ack=1 tx_len=23 "
            "tx_hdr=61880A3412FFFF00112233",
            result,
        )
        runner.parse_line(
            "inet_radio_rx poll=44 done=7 filter=2 crc=1 invalid=3 "
            "phr=24 rejected_len=0 len=23 hdr=61880B3412FFFF44556677 "
            "queue=0/2 overflow=0 receive_at=0/0/1/1/0/0",
            result,
        )
        self.assertTrue(result.radio_snapshot_seen)
        self.assertEqual(result.radio_state, 2)
        self.assertTrue(result.radio_enabled)
        self.assertTrue(result.radio_ready)
        self.assertEqual(result.radio_channel, 15)
        self.assertEqual(result.tx_requests, 12)
        self.assertEqual(result.tx_done, 11)
        self.assertEqual(result.tx_header, "61880A3412FFFF00112233")
        self.assertEqual(result.rx_polls, 44)
        self.assertEqual(result.rx_done, 7)
        self.assertEqual(result.rx_filtered, 2)
        self.assertEqual(result.rx_crc_errors, 1)
        self.assertEqual(result.rx_invalid_lengths, 3)
        self.assertEqual(result.rx_header, "61880B3412FFFF44556677")

    def test_complete_mixed_board_result_passes(self) -> None:
        self.assertTrue(self.validate(*passing_pair()))

    def test_missing_fragmented_payload_fails(self) -> None:
        leader, peer = passing_pair()
        peer.passes.remove(1200)
        self.assertFalse(self.validate(leader, peer))

    def test_payload_failure_overrides_prior_pass(self) -> None:
        leader, peer = passing_pair()
        runner.parse_line(
            "inet_fail len=960 mode=unicast reason=timeout", peer
        )
        self.assertNotIn(960, peer.passes)
        self.assertFalse(self.validate(leader, peer))

    def test_wrong_transport_mode_fails(self) -> None:
        leader, peer = passing_pair()
        peer.pass_modes[960] = "multicast"
        self.assertFalse(self.validate(leader, peer))

    def test_missing_or_failed_discovery_fails(self) -> None:
        leader, peer = passing_pair()
        peer.discovery_passed = False
        self.assertFalse(self.validate(leader, peer))
        runner.parse_line("inet_discovery_fail reason=timeout", peer)
        self.assertEqual(peer.discovery_failure, "timeout")
        self.assertFalse(self.validate(leader, peer))

    def test_completion_summary_failure_is_not_lost(self) -> None:
        leader, peer = passing_pair()
        peer.reported_failures = 1
        self.assertFalse(self.validate(leader, peer))

    def test_dataset_or_wipe_mismatch_fails(self) -> None:
        leader, peer = passing_pair()
        peer.dataset_hex = "00"
        self.assertFalse(self.validate(leader, peer))
        peer.dataset_hex = runner.EXPECTED_DATASET_HEX
        peer.wipe = False
        self.assertFalse(self.validate(leader, peer))

    def test_configured_roles_are_deterministic(self) -> None:
        self.assertEqual(
            runner.configured_role(runner.DEFAULT_FQBN1), "leader"
        )
        self.assertEqual(runner.configured_role(runner.DEFAULT_FQBN2), "child")
        runner.validate_role_pair(runner.DEFAULT_FQBN1, runner.DEFAULT_FQBN2)
        with self.assertRaises(ValueError):
            runner.validate_role_pair(runner.DEFAULT_FQBN1, runner.DEFAULT_FQBN1)

    def test_fqbn_is_forced_to_checkout_vendor(self) -> None:
        fqbn = runner.local_fqbn(
            "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage"
        )
        self.assertEqual(
            fqbn,
            "localnrf54:nrf54l15clean:xiao_nrf54l15:clean_thread=stage",
        )
        with self.assertRaises(ValueError):
            runner.local_fqbn("arduino:avr:uno")

    def test_partition_or_role_mismatch_fails(self) -> None:
        leader, peer = passing_pair()
        peer.partition_id = "DEADBEEF"
        self.assertFalse(self.validate(leader, peer))
        peer.partition_id = leader.partition_id
        peer.role = "leader"
        self.assertFalse(self.validate(leader, peer))


if __name__ == "__main__":
    unittest.main()
