#!/usr/bin/env python3
"""Host-only regression tests for the two-board Thread UDP soak runner."""

from __future__ import annotations

import contextlib
import io
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import test_thread_udp_soak as runner  # noqa: E402


def passing_pair() -> tuple[runner.BoardResults, runner.BoardResults]:
    leader = runner.BoardResults(
        "/dev/leader",
        role="leader",
        rloc16="fc00",
        partition_id="12345678",
        begin_ok=True,
    )
    peer = runner.BoardResults(
        "/dev/peer",
        role="child",
        rloc16="a401",
        partition_id="12345678",
        begin_ok=True,
        done=True,
    )
    for size in runner.SAFE_UNICAST_SIZES:
        peer.unicast[size] = "pass"
    for size in runner.SAFE_DOWNLINK_SIZES:
        leader.downlink[size] = "pass"
    for size in runner.SAFE_MULTICAST_SIZES:
        peer.multicast[size] = "pass"
    return leader, peer


class ThreadUdpSoakRunnerTest(unittest.TestCase):
    def validate(self, board1: runner.BoardResults, board2: runner.BoardResults) -> bool:
        with contextlib.redirect_stdout(io.StringIO()):
            return runner.validate_run(
                board1,
                board2,
                runner.SAFE_UNICAST_SIZES,
                runner.SAFE_DOWNLINK_SIZES,
                runner.SAFE_MULTICAST_SIZES,
            )

    def test_parser_records_repeated_topology_status(self) -> None:
        result = runner.BoardResults("/dev/test")
        runner.parse_line(
            "soak_stat reason=tick role=router rloc16=0xa401 part=0x12345678 "
            "begin_ok=1 mcast_sub=1",
            result,
        )
        self.assertEqual(result.role, "router")
        self.assertEqual(result.rloc16, "a401")
        self.assertEqual(result.partition_id, "12345678")
        self.assertTrue(result.multicast_subscribed)
        self.assertTrue(result.begin_ok)

    def test_partition_reset_discards_stale_results(self) -> None:
        result = runner.BoardResults("/dev/test", done=True)
        result.unicast[8] = "pass"
        result.downlink[8] = "pass"
        result.multicast[8] = "pass"
        runner.parse_line("soak_reset reason=partition-change", result)
        self.assertFalse(result.done)
        self.assertEqual(result.unicast, {})
        self.assertEqual(result.downlink, {})
        self.assertEqual(result.multicast, {})

    def test_valid_bidirectional_owned_results_pass(self) -> None:
        leader, peer = passing_pair()
        self.assertTrue(self.validate(leader, peer))

    def test_other_board_cannot_mask_missing_uplink(self) -> None:
        leader, peer = passing_pair()
        peer.unicast.pop(runner.SAFE_UNICAST_SIZES[0])
        leader.unicast[runner.SAFE_UNICAST_SIZES[0]] = "pass"
        self.assertFalse(self.validate(leader, peer))

    def test_different_partitions_fail(self) -> None:
        leader, peer = passing_pair()
        peer.partition_id = "87654321"
        self.assertFalse(self.validate(leader, peer))

    def test_unobserved_begin_result_fails(self) -> None:
        leader, peer = passing_pair()
        peer.begin_ok = None
        self.assertFalse(self.validate(leader, peer))

    def test_two_leaders_fail(self) -> None:
        leader, peer = passing_pair()
        peer.role = "leader"
        self.assertFalse(self.validate(leader, peer))

    def test_duplicate_or_unsupported_sizes_are_rejected(self) -> None:
        self.assertEqual(runner.parse_size_list("8,8,16", []), [8, 16])
        with self.assertRaises(ValueError):
            runner.parse_size_list("9", [])

    def test_same_physical_port_is_rejected(self) -> None:
        board1 = runner.BoardTarget("board1", "/dev/ttyACM0", "vendor:arch:a")
        board2 = runner.BoardTarget("board2", "/dev/../dev/ttyACM0", "vendor:arch:b")
        with self.assertRaises(ValueError):
            runner.validate_targets(board1, board2)

    def test_fqbn_is_forced_to_checkout_vendor(self) -> None:
        localized = runner.localize_fqbn(
            "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage"
        )
        self.assertEqual(
            localized,
            "localnrf54:nrf54l15clean:xiao_nrf54l15:clean_thread=stage",
        )
        with self.assertRaises(ValueError):
            runner.localize_fqbn("not-an-fqbn")


if __name__ == "__main__":
    unittest.main()
