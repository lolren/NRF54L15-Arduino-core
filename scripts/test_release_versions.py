#!/usr/bin/env python3
"""Regression tests for release-version parsing and generated core metadata."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import build_release
import verify_public_release


ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "hardware" / "nrf54l15clean" / "nrf54l15clean"
GENERATOR_PATH = PLATFORM / "tools" / "generate_core_version_header.py"


def load_core_version_generator():
    spec = importlib.util.spec_from_file_location("core_version_generator", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {GENERATOR_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_index(*versions: str) -> dict:
    return {
        "packages": [
            {
                "platforms": [
                    {"version": version, "architecture": "nrf54l15clean"}
                    for version in versions
                ],
                "tools": [],
            }
        ]
    }


def make_platform_entry(version: str) -> dict:
    return {"version": version, "architecture": "nrf54l15clean"}


class ReleaseVersionTests(unittest.TestCase):
    def test_numeric_and_prerelease_versions(self) -> None:
        self.assertEqual(build_release.parse_version("1.0.1"), (1, 0, 1))
        self.assertEqual(build_release.parse_version("1.0.1-rc1"), (1, 0, 1))
        self.assertEqual(build_release.parse_version("1.0.1-rc.2"), (1, 0, 1))

    def test_invalid_or_ambiguous_versions_are_rejected(self) -> None:
        for version in (
            "1.0",
            "1.0.01",
            "01.0.1",
            "1.0.1-",
            "1.0.1-rc.01",
            " 1.0.1-rc1",
            "1.0.1-rc1 ",
        ):
            with self.subTest(version=version), self.assertRaises(SystemExit):
                build_release.parse_version(version)

    def test_semver_precedence(self) -> None:
        versions = [
            "1.0.1", "1.0.1-rc.10", "0.9.223", "1.0.1-rc.2",
            "1.0.1-beta", "1.0.1-rc1",
        ]
        self.assertEqual(
            sorted(versions, key=build_release.version_sort_key),
            [
                "0.9.223", "1.0.1-beta", "1.0.1-rc.2",
                "1.0.1-rc.10", "1.0.1-rc1", "1.0.1",
            ],
        )

    def test_generated_header_keeps_numeric_parts_and_full_string(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            platform = Path(directory)
            for core in ("nrf54l15", "nrf54lm20b"):
                path = platform / "cores" / core
                path.mkdir(parents=True)
                (path / "CoreVersionGenerated.h").write_text("old\n", encoding="utf-8")
            build_release.update_core_version_header(platform, "1.0.1-rc1")
            for header in sorted((platform / "cores").glob("*/CoreVersionGenerated.h")):
                text = header.read_text(encoding="utf-8")
                self.assertIn("VERSION_MAJOR 1", text)
                self.assertIn("VERSION_MINOR 0", text)
                self.assertIn("VERSION_PATCH 1", text)
                self.assertIn('VERSION_PRERELEASE "rc1"', text)
                self.assertIn("VERSION_IS_PRERELEASE 1", text)
                self.assertIn('VERSION_STRING "1.0.1-rc1"', text)

    def test_standalone_generator_matches_release_builder(self) -> None:
        generator = load_core_version_generator()
        self.assertEqual(generator.parse_version("1.0.1-rc1"), (1, 0, 1))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "CoreVersionGenerated.h"
            subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR_PATH),
                    "--version",
                    "1.0.1-rc1",
                    "--out",
                    str(output),
                ],
                check=True,
            )
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                build_release.render_core_version_header("1.0.1-rc1"),
            )

    def test_standalone_generator_rejects_ambiguous_prerelease(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "CoreVersionGenerated.h"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR_PATH),
                    "--version",
                    "1.0.1-rc.01",
                    "--out",
                    str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertFalse(output.exists())

    def test_prerelease_is_only_added_to_archive_index(self) -> None:
        stable, archive = build_release.prepare_release_indexes(
            existing_full=make_index("1.0.0", "1.0.1-beta"),
            stable_source=make_index("1.0.0", "1.0.1-beta"),
            platform_entry=make_platform_entry("1.0.1-rc1"),
            tool_entry={"name": "host", "version": "1", "systems": []},
            stable_keep=0,
        )
        stable_versions = [
            platform["version"] for platform in stable["packages"][0]["platforms"]
        ]
        archive_versions = [
            platform["version"] for platform in archive["packages"][0]["platforms"]
        ]
        self.assertEqual(stable_versions, ["1.0.0"])
        self.assertEqual(archive_versions, ["1.0.1-rc1", "1.0.1-beta", "1.0.0"])
        self.assertEqual(stable["packages"][0]["tools"], [])
        self.assertEqual(archive["packages"][0]["tools"][0]["name"], "host")

    def test_final_release_is_added_to_stable_and_archive_indexes(self) -> None:
        stable, archive = build_release.prepare_release_indexes(
            existing_full=make_index("1.0.1-rc1", "1.0.0"),
            stable_source=make_index("1.0.1-rc1", "1.0.0"),
            platform_entry=make_platform_entry("1.0.1"),
            tool_entry={"name": "host", "version": "1", "systems": []},
            stable_keep=0,
        )
        stable_versions = [
            platform["version"] for platform in stable["packages"][0]["platforms"]
        ]
        archive_versions = [
            platform["version"] for platform in archive["packages"][0]["platforms"]
        ]
        self.assertEqual(stable_versions, ["1.0.1", "1.0.0"])
        self.assertEqual(archive_versions, ["1.0.1", "1.0.1-rc1", "1.0.0"])
        self.assertEqual(stable["packages"][0]["tools"][0]["name"], "host")

    def test_manifest_records_release_channel(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            build_release.write_release_manifest(
                path,
                version="1.0.1-rc1",
                platform={},
                platform_excludes=(),
                tools=[],
                indexes={},
            )
            manifest = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["channel"], "prerelease")

    def test_public_verifier_uses_platform_host_tool_dependency(self) -> None:
        platform = {
            "toolsDependencies": [
                {
                    "packager": "nrf54l15clean",
                    "name": "nrf54l15hosttools",
                    "version": "1.1.4",
                }
            ]
        }
        self.assertEqual(
            verify_public_release.find_platform_tool_version(
                platform,
                "nrf54l15hosttools",
            ),
            "1.1.4",
        )

    def test_validation_only_is_read_only(self) -> None:
        platform_before = (PLATFORM / "platform.txt").read_bytes()
        headers_before = {
            path: path.read_bytes()
            for path in sorted((PLATFORM / "cores").glob("*/CoreVersionGenerated.h"))
        }
        completed = subprocess.run(
            [
                sys.executable, str(ROOT / "scripts" / "build_release.py"),
                "--root", str(ROOT), "--source-version", "nrf54l15clean",
                "--version", "1.0.1-rc1", "--validate-version-only",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("release version OK: 1.0.1-rc1", completed.stdout)
        self.assertEqual(platform_before, (PLATFORM / "platform.txt").read_bytes())
        for path, content in headers_before.items():
            self.assertEqual(content, path.read_bytes())


if __name__ == "__main__":
    unittest.main()
