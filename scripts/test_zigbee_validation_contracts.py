#!/usr/bin/env python3
"""Regression contracts for the Zigbee hardware-validation entry points."""

import sys
import tempfile
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

from zigbee_validation_common import REPO_ROOT, default_output_dir, write_boolean_summary


VALIDATORS = (
    "zigbee_ha_router_validation.py",
    "zigbee_light_matrix_validation.py",
    "zigbee_rejoin_regression.py",
    "zigbee_sleepy_button_validation.py",
    "zigbee_sleepy_climate_validation.py",
    "zigbee_sleepy_ha_mqtt_validation.py",
    "zigbee_sleepy_ha_validation.py",
)


def validate_result_model() -> None:
    with tempfile.TemporaryDirectory() as directory:
        summary_path = Path(directory) / "summary.txt"

        assert write_boolean_summary(
            summary_path,
            {"joined": True, "interview_failed": False},
            expected_false={"interview_failed"},
        ) == 0
        assert summary_path.read_text() == (
            "joined=true\ninterview_failed=false\n"
        )

        assert write_boolean_summary(summary_path, {"joined": False}) == 1
        assert write_boolean_summary(summary_path, {}) == 1
        assert write_boolean_summary(
            summary_path,
            {"interview_failed": True},
            expected_false={"interview_failed"},
        ) == 1

    expected_output = REPO_ROOT / ".build" / "contract-test"
    assert Path(default_output_dir("contract-test")) == expected_output
    print("PASS validator result and repository-relative output model")


def validate_entry_points() -> None:
    obsolete_workspace = "/home/lolren/Desktop/Nrf54L15"
    for filename in VALIDATORS:
        source = (SCRIPTS / filename).read_text()
        assert obsolete_workspace not in source, (
            f"{filename} contains the obsolete workspace path"
        )
        assert 'if __name__ == "__main__":' in source
        assert "raise SystemExit(main())" in source, (
            f"{filename} does not propagate main() failure to the shell"
        )

    matrix = (SCRIPTS / "zigbee_light_matrix_validation.py").read_text()
    assert "subprocess.run(cmd, check=True)" in matrix
    assert "bool(overall)" in matrix
    assert "bool(result) and all(result.values())" in matrix

    mqtt = (SCRIPTS / "zigbee_sleepy_ha_mqtt_validation.py").read_text()
    assert 'expected_false={"z2m_interview_failed"}' in mqtt
    print("PASS validator entry-point, subprocess, and expectation contracts")


def main() -> int:
    validate_result_model()
    validate_entry_points()
    print("PASS all Zigbee hardware-validation contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
