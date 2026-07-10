#!/usr/bin/env python3
"""Focused regression tests for the Arduino upload wrapper."""

import importlib.util
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
UPLOAD_PATH = (
    ROOT
    / "hardware"
    / "nrf54l15clean"
    / "nrf54l15clean"
    / "tools"
    / "upload.py"
)
PLATFORM_PATH = UPLOAD_PATH.parents[1] / "platform.txt"
UF2_EMITTER = UPLOAD_PATH.parent / "uf2" / "uf2_emit.py"
SPEC = importlib.util.spec_from_file_location("nrf54_upload", UPLOAD_PATH)
UPLOAD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = UPLOAD
SPEC.loader.exec_module(UPLOAD)


def result(returncode: int) -> subprocess.CompletedProcess:
    return subprocess.CompletedProcess(["pyocd", "erase"], returncode, "", "")


class ProtectedTargetRecoveryTests(unittest.TestCase):
    @mock.patch.object(UPLOAD, "time")
    @mock.patch.object(UPLOAD, "recover_target")
    def test_retries_after_first_erase_failure(self, recover, _time):
        recover.side_effect = [result(1), result(0)]

        actual = UPLOAD.recover_target_with_retries(
            ["pyocd"], "nrf54l", "probe", retry_delay=0
        )

        self.assertEqual(actual.returncode, 0)
        self.assertEqual(recover.call_count, 2)
        self.assertIsNone(recover.call_args_list[1].kwargs["connect_mode"])

    @mock.patch.object(UPLOAD, "recover_target")
    def test_stops_after_first_success(self, recover):
        recover.return_value = result(0)

        actual = UPLOAD.recover_target_with_retries(
            ["pyocd"], "nrf54l", "probe", attempts=3
        )

        self.assertEqual(actual.returncode, 0)
        recover.assert_called_once()


class PlatformRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.properties = {}
        for raw_line in PLATFORM_PATH.read_text(encoding="utf-8").splitlines():
            key, separator, value = raw_line.partition("=")
            if separator:
                cls.properties[key] = value

    def test_windows_native_upload_routes_through_recovery_wrapper(self):
        pattern = self.properties["tools.nrf54ocd.upload.pattern.windows"]
        self.assertIn("powershell", pattern.lower())
        self.assertIn("upload_windows.ps1", pattern)
        self.assertIn('-HexPath "{build.path}/{build.project_name}.hex"', pattern)
        self.assertNotIn("{tools.python3", pattern)
        self.assertNotIn("upload.py", pattern)

    def test_uf2_recipe_runs_the_real_emitter(self):
        pattern = self.properties["recipe.objcopy.uf2.pattern"]
        self.assertIn("{tools.nrf54uf2.cmd.path}", pattern)
        self.assertIn('--input "{build.path}/{build.project_name}.hex"', pattern)
        self.assertIn('--output "{build.path}/{build.project_name}.uf2"', pattern)

    def test_windows_uf2_recipe_is_native_and_uses_binary_input(self):
        pattern = self.properties["recipe.objcopy.uf2.pattern.windows"]
        self.assertIn("powershell", pattern.lower())
        self.assertIn("uf2_emit.ps1", pattern)
        self.assertIn('-InputPath "{build.path}/{build.project_name}.bin"', pattern)
        self.assertNotIn("{tools.python3", pattern)

    def test_programmer_recipe_selects_pyocd(self):
        pattern = self.properties["tools.nrf54program.program.pattern"]
        self.assertIn('--runner "pyocd"', pattern)
        self.assertNotIn("{programmer.", pattern)

    @unittest.skipUnless(
        shutil.which("arduino-cli") and Path("/bin/echo").is_file(),
        "Arduino CLI expansion test requires arduino-cli and /bin/echo",
    )
    def test_arduino_cli_expands_programmer_recipe(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-programmer-test-") as directory:
            root = Path(directory)
            user = root / "user"
            data = root / "data"
            downloads = root / "downloads"
            hardware = user / "hardware" / "localnrf54"
            hardware.mkdir(parents=True)
            data.mkdir()
            downloads.mkdir()
            (hardware / "nrf54l15clean").symlink_to(
                UPLOAD_PATH.parents[1], target_is_directory=True
            )
            config = root / "arduino-cli.yaml"
            config.write_text(
                "directories:\n"
                f"  data: {data}\n"
                f"  downloads: {downloads}\n"
                f"  user: {user}\n",
                encoding="utf-8",
            )
            image = root / "RecipeProbe.ino.hex"
            image.write_text(":00000001FF\n", encoding="ascii")
            completed = subprocess.run(
                [
                    "arduino-cli",
                    "upload",
                    "--config-file",
                    str(config),
                    "--fqbn",
                    "localnrf54:nrf54l15clean:xiao_nrf54l15",
                    "-P",
                    "pyocd",
                    "--input-file",
                    str(image),
                    "-p",
                    "/dev/null",
                    "--upload-property",
                    "tools.python3.cmd=/bin/echo",
                    "-v",
                ],
                capture_output=True,
                text=True,
            )
            output = completed.stdout + completed.stderr
            self.assertEqual(completed.returncode, 0, output)
            self.assertIn("--runner pyocd", output.replace('"', ""))
            self.assertNotIn("{programmer.", output)

    def test_uf2_emitter_produces_valid_blocks(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-uf2-test-") as directory:
            source = Path(directory) / "probe.hex"
            output = Path(directory) / "probe.uf2"
            source.write_text(
                ":020000040000FA\n:0400000001020304F2\n:00000001FF\n",
                encoding="ascii",
            )
            subprocess.run(
                [
                    sys.executable,
                    str(UF2_EMITTER),
                    "--input",
                    str(source),
                    "--output",
                    str(output),
                    "--family",
                    "0xADA54B15",
                    "--base-address",
                    "0",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            data = output.read_bytes()
            self.assertGreaterEqual(len(data), 512)
            self.assertEqual(len(data) % 512, 0)
            magic0, magic1 = struct.unpack_from("<II", data, 0)
            magic_end = struct.unpack_from("<I", data, 508)[0]
            self.assertEqual(magic0, 0x0A324655)
            self.assertEqual(magic1, 0x9E5D5157)
            self.assertEqual(magic_end, 0x0AB16F30)


if __name__ == "__main__":
    unittest.main()
