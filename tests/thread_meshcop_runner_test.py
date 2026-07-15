#!/usr/bin/env python3
"""Host-only regression tests for the MeshCoP validation runner."""

from __future__ import annotations

import sys
import unittest
from argparse import Namespace
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import thread_meshcop_validation as runner  # noqa: E402


class ThreadMeshcopRunnerTest(unittest.TestCase):
    commissioner_ready = (
        "thread_commissioner commissioner_active=1",
        "thread_commissioner joiner_entry_added=1",
        "thread_commissioner meshcop_event_count=2",
        "thread_commissioner meshcop_finalize_count=0",
    )

    def validate(self, *joiner_lines: str, commissioner=None) -> bool:
        commissioner_lines = list(
            self.commissioner_ready if commissioner is None else commissioner
        )
        return runner.validate_wrong_pskd_lines(
            commissioner_lines, list(joiner_lines)
        ).ok

    def test_explicit_expected_failure_passes(self) -> None:
        self.assertTrue(
            self.validate(
                "thread_meshcop_wrong_pskd joiner_callback error=8",
                "thread_meshcop_wrong_pskd active_dataset=0",
            )
        )

    def test_persistent_security_callback_error_passes(self) -> None:
        self.assertTrue(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=1 callback_error=8 "
                "unexpected_success=0 active_dataset=0"
            )
        )

    def test_callback_seen_without_failure_error_does_not_pass(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=1 callback_error=0 "
                "unexpected_success=0 active_dataset=0"
            )
        )

    def test_no_commissioner_not_found_error_does_not_pass(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=1 callback_error=23 "
                "unexpected_success=0 active_dataset=0"
            )
        )

    def test_timeout_without_callback_does_not_pass(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=0 callback_error=0 "
                "unexpected_success=0 active_dataset=0"
            )
        )

    def test_missing_commissioner_attempt_evidence_does_not_pass(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=1 callback_error=8 "
                "unexpected_success=0 active_dataset=0",
                commissioner=(
                    "thread_commissioner commissioner_active=1",
                    "thread_commissioner joiner_entry_added=1",
                    "thread_commissioner meshcop_event_count=0",
                ),
            )
        )

    def test_commissioner_finalize_evidence_does_not_pass(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=1 callback_error=8 "
                "unexpected_success=0 active_dataset=0",
                commissioner=(
                    "thread_commissioner commissioner_active=1",
                    "thread_commissioner joiner_entry_added=1",
                    "thread_commissioner meshcop_event_count=3",
                    "thread_commissioner meshcop_finalize_count=1",
                ),
            )
        )

    def test_persistent_unexpected_success_cannot_be_hidden(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd callback_seen=1 callback_error=0 "
                "unexpected_success=1 active_dataset=0"
            )
        )

    def test_one_shot_unexpected_join_success_fails(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd joiner_callback error=8",
                "thread_meshcop_wrong_pskd unexpected_join_success=1",
                "thread_meshcop_wrong_pskd active_dataset=0",
            )
        )

    def test_persisted_dataset_fails(self) -> None:
        self.assertFalse(
            self.validate(
                "thread_meshcop_wrong_pskd joiner_callback error=8",
                "thread_meshcop_wrong_pskd active_dataset=1",
            )
        )

    def test_reboot_restore_requires_all_persistence_evidence(self) -> None:
        result = runner.validate_restore_lines(
            [
                "thread_joiner restore_attempted=1",
                "thread_joiner restore_restored=1",
                "thread_joiner dataset_configured=1",
                "thread_joiner reboot_restore_ready=1",
                "thread_joiner status_active_dataset=1",
            ]
        )
        self.assertTrue(result.ok)

    def test_reboot_restore_rejects_missing_dataset(self) -> None:
        result = runner.validate_restore_lines(
            [
                "thread_joiner restore_attempted=1",
                "thread_joiner restore_restored=0",
                "thread_joiner dataset_configured=0",
                "thread_joiner status_active_dataset=0",
            ]
        )
        self.assertFalse(result.ok)

    def test_reboot_restore_rejects_meshcop_restart(self) -> None:
        result = runner.validate_restore_lines(
            [
                "thread_joiner restore_attempted=1",
                "thread_joiner restore_restored=1",
                "thread_joiner dataset_configured=1",
                "thread_joiner reboot_restore_mode=1",
                "thread_joiner status_active_dataset=1",
                "thread_joiner joiner_start=1",
            ]
        )
        self.assertFalse(result.ok)

    def test_fresh_join_chip_erases_joiner_before_upload(self) -> None:
        args = Namespace(skip_upload=False)
        commissioner = runner.BoardTarget(
            "commissioner", "/dev/commissioner", "vendor:arch:l15", "L15"
        )
        joiner = runner.BoardTarget(
            "joiner", "/dev/joiner", "vendor:arch:lm20a", "LM20A"
        )
        events = []

        def fake_erase(target, *_args):
            events.append(f"erase:{target.label}")
            return runner.StepResult("erase joiner", True)

        def fake_upload(name, target, *_args):
            events.append(f"upload:{name}:{target.label}")
            return runner.StepResult(f"upload {name}", True)

        captured = {
            "commissioner": ["thread_commissioner JOINER_ACCEPTED"],
            "joiner": [
                "thread_joiner JOIN_SUCCESS",
                "thread_joiner preexisting_dataset_before_joiner=0",
            ],
        }
        with mock.patch.object(runner, "erase_target", side_effect=fake_erase), \
             mock.patch.object(runner, "upload_sketch", side_effect=fake_upload), \
             mock.patch.object(runner, "capture_serial", return_value=captured), \
             mock.patch.object(runner.time, "sleep"):
            result = runner.run_fresh_join(
                args, {}, Path("/tmp/log"), commissioner, joiner
            )

        self.assertTrue(result.ok)
        self.assertEqual(
            events,
            [
                "erase:joiner",
                "upload:commissioner:commissioner",
                "upload:joiner:joiner",
            ],
        )

    def test_fresh_join_stops_when_chip_erase_fails(self) -> None:
        args = Namespace(skip_upload=False)
        commissioner = runner.BoardTarget(
            "commissioner", "/dev/commissioner", "vendor:arch:l15", "L15"
        )
        joiner = runner.BoardTarget(
            "joiner", "/dev/joiner", "vendor:arch:lm20a", "LM20A"
        )
        with mock.patch.object(
            runner,
            "erase_target",
            return_value=runner.StepResult("erase joiner", False, ["failed"]),
        ), mock.patch.object(runner, "upload_sketch") as upload:
            result = runner.run_fresh_join(
                args, {}, Path("/tmp/log"), commissioner, joiner
            )

        self.assertFalse(result.ok)
        upload.assert_not_called()

    def test_mixed_board_roles_resolve_with_their_own_fqbn_and_uid(self) -> None:
        args = Namespace(
            fqbn="",
            commissioner_fqbn="vendor:arch:l15",
            joiner_fqbn="vendor:arch:lm20a",
            commissioner_port="/dev/l15",
            commissioner_uid="L15",
            joiner_port="/dev/lm20a",
            joiner_uid="LM20A",
        )

        def fake_resolve(label, port, uid, fqbn, fallback):
            self.assertEqual(port, fallback)
            return runner.BoardTarget(label, port, fqbn, uid)

        with mock.patch.object(runner, "resolve_target", side_effect=fake_resolve):
            commissioner, joiner = runner.resolve_hardware_targets(args)

        self.assertEqual(commissioner.fqbn, "localnrf54:arch:l15")
        self.assertEqual(commissioner.uid, "L15")
        self.assertEqual(joiner.fqbn, "localnrf54:arch:lm20a")
        self.assertEqual(joiner.uid, "LM20A")

    def test_hardware_resolution_requires_each_role_identity(self) -> None:
        args = Namespace(
            commissioner_port="",
            commissioner_uid="",
            joiner_port="/dev/joiner",
            joiner_uid="",
        )
        with self.assertRaisesRegex(ValueError, "commissioner"):
            runner.resolve_hardware_targets(args)

    def test_same_physical_target_is_rejected(self) -> None:
        commissioner = runner.BoardTarget(
            "commissioner", "/dev/ttyACM0", "vendor:arch:l15", "SAME"
        )
        joiner = runner.BoardTarget(
            "joiner", "/dev/../dev/ttyACM0", "vendor:arch:lm20a", "SAME"
        )
        with self.assertRaises(ValueError):
            runner.validate_targets(commissioner, joiner)


if __name__ == "__main__":
    unittest.main()
