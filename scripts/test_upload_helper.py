#!/usr/bin/env python3
"""Focused regression tests for the Arduino upload wrapper."""

import importlib.util
import io
import os
import re
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
BOARDS_PATH = PLATFORM_PATH.with_name("boards.txt")
PLATFORM_TOOLS = UPLOAD_PATH.parent
HOST_TOOLS = ROOT / "tools" / "board_manager" / "nrf54l15hosttools"
LEGACY_HOST_TOOLS = PLATFORM_TOOLS / "nrf54l15hosttools" / "1.1.3"
UF2_EMITTER = UPLOAD_PATH.parent / "uf2" / "uf2_emit.py"
UF2_CONV = UPLOAD_PATH.parent / "uf2" / "uf2conv.py"
SPEC = importlib.util.spec_from_file_location("nrf54_upload", UPLOAD_PATH)
UPLOAD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = UPLOAD
SPEC.loader.exec_module(UPLOAD)
UF2_SPEC = importlib.util.spec_from_file_location("nrf54_uf2conv", UF2_CONV)
UF2_CONV_MODULE = importlib.util.module_from_spec(UF2_SPEC)
assert UF2_SPEC.loader is not None
sys.modules[UF2_SPEC.name] = UF2_CONV_MODULE
UF2_SPEC.loader.exec_module(UF2_CONV_MODULE)


def result(returncode: int) -> subprocess.CompletedProcess:
    return subprocess.CompletedProcess(["pyocd", "erase"], returncode, "", "")


class NrfOcdTransportTests(unittest.TestCase):
    def test_bundled_binary_wins_over_shorter_old_cache_path(self):
        executable_name = "nrf_ocd.exe" if sys.platform.startswith("win") else "nrf_ocd"
        with tempfile.TemporaryDirectory(prefix="nrf54-ocd-selection-") as directory:
            root = Path(directory)
            script_dir = root / "current-release-with-a-deliberately-long-path" / "tools"
            script_dir.mkdir(parents=True)
            bundled = script_dir / executable_name
            bundled.touch()

            old_cache = root / "old"
            old_cache.mkdir()
            (old_cache / executable_name).touch()

            with (
                mock.patch.object(UPLOAD, "__file__", str(script_dir / "upload.py")),
                mock.patch.dict(
                    os.environ,
                    {"NRF54_NRF_OCD": "", "OPEN_NRF_OCD": ""},
                ),
            ):
                actual = UPLOAD.detect_nrf_ocd_command(
                    host_tools_path=old_cache,
                    allow_download=False,
                )

        self.assertEqual(actual, [str(bundled)])

    def test_environment_override_wins_over_bundled_binary(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-ocd-override-") as directory:
            script_dir = Path(directory)
            (script_dir / "nrf_ocd").touch()
            with (
                mock.patch.object(UPLOAD, "__file__", str(script_dir / "upload.py")),
                mock.patch.dict(
                    os.environ,
                    {"NRF54_NRF_OCD": "/custom/nrf_ocd --probe exact"},
                    clear=False,
                ),
            ):
                actual = UPLOAD.detect_nrf_ocd_command(allow_download=False)

        self.assertEqual(actual, ["/custom/nrf_ocd", "--probe", "exact"])

    def test_zero_exit_transport_error_requests_pyocd_fallback(self):
        fake_nrf_ocd = [
            sys.executable,
            "-c",
            (
                "print('08:10:55 ERROR cmsis_dap.c:54 bulk write failed "
                "for cmd 0x03: I/O error', flush=True)"
            ),
        ]

        actual = UPLOAD.upload_nrf_ocd(
            "fixture.hex",
            "nrf54lm20a",
            "140EBF71",
            nrf_ocd_cmd=fake_nrf_ocd,
        )

        self.assertEqual(actual, UPLOAD.NRF_OCD_TRANSPORT_FALLBACK)
        self.assertNotEqual(actual, 0)


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


class LinuxProbePermissionTests(unittest.TestCase):
    def test_lm20a_pyocd_uses_external_registration_without_mutating_pyocd(self):
        source = UPLOAD_PATH.read_text(encoding="utf-8")
        self.assertFalse((PLATFORM_TOOLS / "patch_lm20b_target.py").exists())
        self.assertNotIn("patch_lm20b_target", source)
        command = UPLOAD.append_pyocd_target_script(["pyocd", "load"], "nrf54lm20a")
        self.assertEqual(
            command,
            [
                "pyocd",
                "load",
                "--script",
                str(PLATFORM_TOOLS / "pyocd_register_lm20b.py"),
            ],
        )

    def test_every_shipped_udev_rule_covers_both_xiao_probe_ids(self):
        for rule in (
            HOST_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules",
            PLATFORM_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules",
            LEGACY_HOST_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules",
        ):
            source = rule.read_text(encoding="utf-8")
            with self.subTest(rule=rule):
                for product in ("0066", "0068"):
                    self.assertEqual(source.count(f'idProduct}}=="{product}"'), 3)

    def test_every_linux_installer_retriggers_both_xiao_probe_ids(self):
        for installer in (
            HOST_TOOLS / "setup/install_linux_host_deps.sh",
            PLATFORM_TOOLS / "setup/install_linux_host_deps.sh",
            LEGACY_HOST_TOOLS / "setup/install_linux_host_deps.sh",
        ):
            source = installer.read_text(encoding="utf-8")
            with self.subTest(installer=installer):
                for product in ("0066", "0068"):
                    trigger = (
                        "trigger --attr-match=idVendor=2886 "
                        f"--attr-match=idProduct={product}"
                    )
                    self.assertGreaterEqual(source.count(trigger), 2)

    def test_canonical_udev_installer_executes_both_triggers(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-udev-test-") as directory:
            root = Path(directory)
            rule_dir = root / "rules.d"
            rule_dir.mkdir()
            log = root / "udevadm.log"
            fake_udevadm = root / "udevadm"
            fake_udevadm.write_text(
                "#!/usr/bin/env bash\n"
                'printf "%s\\n" "$*" >>"${UDEVADM_TEST_LOG}"\n',
                encoding="utf-8",
            )
            fake_udevadm.chmod(0o755)
            destination = rule_dir / "60-seeed-xiao-nrf54-cmsis-dap.rules"
            environment = os.environ.copy()
            environment.update(
                {
                    "RULE_DST": str(destination),
                    "UDEVADM_BIN": str(fake_udevadm),
                    "UDEVADM_TEST_LOG": str(log),
                }
            )

            completed = subprocess.run(
                [str(HOST_TOOLS / "setup/install_linux_host_deps.sh"), "--udev"],
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            commands = log.read_text(encoding="utf-8")
            self.assertIn("idProduct=0066", commands)
            self.assertIn("idProduct=0068", commands)
            self.assertEqual(destination.read_bytes(), (
                HOST_TOOLS / "setup/60-seeed-xiao-nrf54-cmsis-dap.rules"
            ).read_bytes())

    def test_permission_hint_reports_the_connected_lm20a_product_id(self):
        denied = subprocess.CompletedProcess(
            ["pyocd", "list"], 1, "", "No connected debug probes"
        )
        stderr = io.StringIO()
        with (
            mock.patch.object(UPLOAD.sys, "platform", "linux"),
            mock.patch.object(UPLOAD, "tool_available", return_value=True),
            mock.patch.object(
                UPLOAD,
                "run",
                return_value=subprocess.CompletedProcess(
                    ["lsusb"], 0, "Bus 001 Device 002: ID 2886:0068 Seeed\n", ""
                ),
            ),
            mock.patch.object(
                UPLOAD, "matching_probe_hidraw_nodes", return_value=[Path("/dev/hidraw0")]
            ),
            mock.patch.object(UPLOAD, "probe_hidraw_nodes_accessible", return_value=False),
            mock.patch.object(UPLOAD.sys, "stderr", stderr),
        ):
            UPLOAD.print_linux_probe_permission_hint(denied)

        self.assertIn("2886:0068", stderr.getvalue())
        self.assertNotIn("2886:0066 is present", stderr.getvalue())

    def test_preflight_reports_the_product_id_from_hidraw_sysfs(self):
        node = Path("/dev/hidraw0")
        stderr = io.StringIO()
        with (
            mock.patch.object(UPLOAD.sys, "platform", "linux"),
            mock.patch.object(UPLOAD, "matching_probe_hidraw_nodes", return_value=[node]),
            mock.patch.object(UPLOAD, "probe_hidraw_nodes_accessible", return_value=False),
            mock.patch.object(
                UPLOAD,
                "_sysfs_usb_identity_for_hidraw",
                return_value=("2886", "0068"),
            ),
            mock.patch.object(UPLOAD.sys, "stderr", stderr),
        ):
            blocked = UPLOAD.preflight_linux_probe_access(None)

        self.assertTrue(blocked)
        self.assertIn("2886:0068", stderr.getvalue())


class PlatformRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.properties = {}
        for raw_line in PLATFORM_PATH.read_text(encoding="utf-8").splitlines():
            key, separator, value = raw_line.partition("=")
            if separator:
                cls.properties[key] = value
        cls.board_properties = {}
        for raw_line in BOARDS_PATH.read_text(encoding="utf-8").splitlines():
            key, separator, value = raw_line.partition("=")
            if separator:
                cls.board_properties[key] = value
        cls.board_ids = sorted(
            key.removesuffix(".name")
            for key in cls.board_properties
            if key.endswith(".name")
        )

    def test_legacy_ide_verbosity_properties_are_defined_as_no_ops(self):
        property_names = (
            "tools.nrf54ocd.upload.params.verbose",
            "tools.nrf54ocd.upload.params.quiet",
            "tools.nrf54upload.upload.params.verbose",
            "tools.nrf54upload.upload.params.quiet",
            "tools.nrf54program.program.params.verbose",
            "tools.nrf54program.program.params.quiet",
        )
        for property_name in property_names:
            with self.subTest(property=property_name):
                self.assertIn(property_name, self.properties)
                self.assertEqual("", self.properties[property_name])

    def test_python_recipes_do_not_depend_on_legacy_unresolved_tool_properties(self):
        recipe_keys = (
            "recipe.hooks.prebuild.1.pattern",
            "recipe.objcopy.uf2.pattern",
            "tools.nrf54ocd.upload.pattern",
            "tools.nrf54ocd.upload.pattern.macosx",
            "tools.nrf54upload.upload.pattern",
            "tools.nrf54program.program.pattern",
        )
        for recipe_key in recipe_keys:
            with self.subTest(recipe=recipe_key):
                self.assertNotIn("{tools.python3", self.properties[recipe_key])
                self.assertIn("python3", self.properties[recipe_key])

    def test_python_recipes_use_runtime_platform_paths_for_arduino_ide_1(self):
        recipe_keys = (
            "recipe.objcopy.uf2.pattern",
            "recipe.objcopy.uf2.pattern.windows",
            "tools.nrf54ocd.upload.pattern",
            "tools.nrf54ocd.upload.pattern.macosx",
            "tools.nrf54upload.upload.pattern",
            "tools.nrf54program.program.pattern",
        )
        for recipe_key in recipe_keys:
            with self.subTest(recipe=recipe_key):
                pattern = self.properties[recipe_key]
                self.assertNotIn("{tools.", pattern)
                self.assertIn("{runtime.platform.path}/tools/", pattern)

        self.assertEqual(
            self.properties["tools.nrf54upload.cmd.path"],
            "{runtime.platform.path}/tools/upload.py",
        )
        self.assertEqual(
            self.properties["tools.nrf54program.cmd.path"],
            "{runtime.platform.path}/tools/upload.py",
        )

    def test_upload_recipe_properties_are_board_scoped_for_arduino_ide_1(self):
        recipe_keys = (
            "tools.nrf54ocd.upload.pattern",
            "tools.nrf54ocd.upload.pattern.macosx",
            "tools.nrf54upload.upload.pattern",
            "tools.nrf54program.program.pattern",
        )
        upload_properties = set()
        for recipe_key in recipe_keys:
            upload_properties.update(
                re.findall(r"\{upload\.([^}]+)\}", self.properties[recipe_key])
            )

        self.assertEqual(
            {
                "runner",
                "target",
                "uid",
                "openocd_script",
                "openocd_speed",
                "uf2_drive",
                "uf2_labels",
                "uf2_timeout",
                "pyocd_safe",
            },
            upload_properties,
        )
        self.assertGreater(len(self.board_ids), 0)
        for board_id in self.board_ids:
            with self.subTest(board=board_id):
                for upload_property in upload_properties:
                    self.assertIn(
                        f"{board_id}.upload.{upload_property}",
                        self.board_properties,
                    )
                self.assertEqual(
                    self.board_properties[f"{board_id}.upload.uf2_timeout"],
                    self.properties["upload.uf2_timeout"],
                )
                self.assertEqual(
                    self.board_properties[f"{board_id}.upload.uf2_labels"],
                    self.properties["upload.uf2_labels"],
                )

    def test_windows_native_upload_routes_through_recovery_wrapper(self):
        pattern = self.properties["tools.nrf54ocd.upload.pattern.windows"]
        self.assertIn("powershell", pattern.lower())
        self.assertIn("upload_windows.ps1", pattern)
        self.assertIn('-HexPath "{build.path}/{build.project_name}.hex"', pattern)
        # Every shipped board leaves upload.uid empty. Passing it through the
        # native command line renders `-Uid ""`, which powershell.exe treats as
        # a missing parameter argument before upload_windows.ps1 can run.
        self.assertNotIn("-Uid", pattern)
        self.assertNotIn("{upload.uid}", pattern)
        self.assertNotIn("{tools.python3", pattern)
        self.assertNotIn("upload.py", pattern)

    def test_windows_native_upload_stops_after_the_resetting_load(self):
        source = (PLATFORM_TOOLS / "upload_windows.ps1").read_text(encoding="utf-8")
        self.assertNotIn("0xE000EDF0", source)
        self.assertNotIn("$detachArgs", source)
        self.assertNotIn("$detachExitCode", source)

    def test_uf2_recipe_runs_the_real_emitter(self):
        pattern = self.properties["recipe.objcopy.uf2.pattern"]
        self.assertIn("{runtime.platform.path}/tools/uf2/uf2_emit.py", pattern)
        self.assertIn('--input "{build.path}/{build.project_name}.hex"', pattern)
        self.assertIn('--output "{build.path}/{build.project_name}.uf2"', pattern)

    def test_windows_uf2_recipe_is_native_and_uses_binary_input(self):
        pattern = self.properties["recipe.objcopy.uf2.pattern.windows"]
        self.assertIn("powershell", pattern.lower())
        self.assertIn("uf2_emit.ps1", pattern)
        self.assertIn('-InputPath "{build.path}/{build.project_name}.bin"', pattern)
        self.assertNotIn("{tools.python3", pattern)

    def test_windows_uf2_emitter_uses_unsigned_constants(self):
        script = (UF2_EMITTER.parent / "uf2_emit.ps1").read_text(encoding="utf-8")
        self.assertIn('$uf2MagicStart1 = Convert-ToUInt32 "0x9E5D5157"', script)
        self.assertNotIn("Set-UInt32LE $block 4 0x9E5D5157", script)
        self.assertNotIn("Set-UInt32LE $block 28 0xADA54B15", script)

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
            fake_bin = root / "bin"
            hardware = user / "hardware" / "localnrf54"
            hardware.mkdir(parents=True)
            data.mkdir()
            downloads.mkdir()
            fake_bin.mkdir()
            fake_python = fake_bin / "python3"
            fake_python.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\"\n", encoding="utf-8")
            fake_python.chmod(0o755)
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
                    "-v",
                ],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
                },
            )
            output = completed.stdout + completed.stderr
            self.assertEqual(completed.returncode, 0, output)
            self.assertIn("--runner pyocd", output.replace('"', ""))
            self.assertNotIn("{programmer.", output)
            self.assertNotIn("{upload.", output)

    @unittest.skipUnless(
        shutil.which("arduino-cli") and Path("/bin/echo").is_file(),
        "Arduino CLI expansion test requires arduino-cli and /bin/echo",
    )
    def test_arduino_cli_expands_default_upload_in_quiet_and_verbose_modes(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-upload-test-") as directory:
            root = Path(directory)
            user = root / "user"
            data = root / "data"
            downloads = root / "downloads"
            fake_bin = root / "bin"
            hardware = user / "hardware" / "localnrf54"
            hardware.mkdir(parents=True)
            data.mkdir()
            downloads.mkdir()
            fake_bin.mkdir()
            fake_python = fake_bin / "python3"
            fake_python.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\"\n", encoding="utf-8")
            fake_python.chmod(0o755)
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
            command = [
                "arduino-cli",
                "upload",
                "--config-file",
                str(config),
                "--fqbn",
                "localnrf54:nrf54l15clean:xiao_nrf54l15",
                "--input-file",
                str(image),
                "-p",
                "/dev/null",
            ]

            for verbose_args in ([], ["-v"]):
                with self.subTest(verbose=bool(verbose_args)):
                    completed = subprocess.run(
                        [*command, *verbose_args],
                        capture_output=True,
                        text=True,
                        env={
                            **os.environ,
                            "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
                        },
                    )
                    output = completed.stdout + completed.stderr
                    self.assertEqual(0, completed.returncode, output)
                    self.assertRegex(output.replace('"', ""), r"--runner\s+nrf_ocd")
                    self.assertIn("/tools/upload.py", output)
                    self.assertNotIn("{tools.", output)
                    self.assertNotIn("{upload.", output)
                    self.assertRegex(output.replace('"', ""), r"--uf2-timeout\s+12")
                    self.assertNotIn("--openocd-bin", output)
                    self.assertNotIn("{upload.verbose}", output)

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

    def test_uf2_deploy_write_rejects_device_root_escape(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-uf2-root-") as directory:
            root = Path(directory) / "drive"
            root.mkdir()
            outside = Path(directory) / "NEW.UF2"

            with self.assertRaises(ValueError):
                UF2_CONV_MODULE.write_file(str(root / ".." / "NEW.UF2"), b"bad", root=root)

            self.assertFalse(outside.exists())

    def test_uf2_deploy_write_allows_device_root_child(self):
        with tempfile.TemporaryDirectory(prefix="nrf54-uf2-root-") as directory:
            root = Path(directory) / "drive"
            root.mkdir()
            output = root / "NEW.UF2"

            UF2_CONV_MODULE.write_file(str(output), b"ok", root=root)

            self.assertEqual(output.read_bytes(), b"ok")


if __name__ == "__main__":
    unittest.main()
