#!/usr/bin/env python3
"""Regression tests for release-version parsing and generated core metadata."""

from __future__ import annotations

import importlib.util
import hashlib
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
    def test_release_staging_excludes_ignored_workspace_files(self) -> None:
        ignored = PLATFORM / "build" / "release-ignore-probe.bin"
        ignored.parent.mkdir(parents=True, exist_ok=True)
        ignored.write_bytes(b"must not ship")
        try:
            with tempfile.TemporaryDirectory() as directory:
                stage = Path(directory) / PLATFORM.name
                build_release.stage_git_release_tree(ROOT, PLATFORM, stage)
                self.assertTrue((stage / "platform.txt").is_file())
                self.assertFalse((stage / "build" / ignored.name).exists())
        finally:
            ignored.unlink(missing_ok=True)
            try:
                ignored.parent.rmdir()
            except OSError:
                pass

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
                    "version": build_release.HOST_TOOL_VERSION,
                }
            ]
        }
        self.assertEqual(
            verify_public_release.find_platform_tool_version(
                platform,
                "nrf54l15hosttools",
            ),
            build_release.HOST_TOOL_VERSION,
        )

    def test_hosttools_requirements_are_pinned_without_redistributed_wheels(self) -> None:
        hosttools = ROOT / "tools" / "board_manager" / build_release.HOST_TOOL_NAME
        build_release.validate_host_tool_source(hosttools)
        requirements = (hosttools / build_release.HOST_TOOL_REQUIREMENTS_FILE).read_text(
            encoding="utf-8"
        )
        for requirement in build_release.REQUIRED_HOST_TOOL_REQUIREMENTS:
            self.assertIn(requirement, requirements.splitlines())
        self.assertEqual(list(hosttools.rglob("*.whl")), [])

    def test_release_workflow_uses_curated_immutable_release_notes(self) -> None:
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        for mutable_ref in (
            "actions/checkout@v4",
            "actions/setup-python@v5",
            "arduino/setup-arduino-cli@v2",
        ):
            self.assertNotIn(mutable_ref, workflow)
        self.assertIn('NOTES="docs/RELEASE_${VERSION}.md"', workflow)
        self.assertIn('test -f "${NOTES}"', workflow)
        self.assertIn("body_path: ${{ steps.vars.outputs.notes }}", workflow)
        self.assertIn("generate_release_notes: true", workflow)
        publish = workflow.index("uses: softprops/action-gh-release@")
        verify = workflow.index("name: Verify draft release assets", publish)
        self.assertLess(
            workflow.index("body_path: ${{ steps.vars.outputs.notes }}", publish), verify
        )

    def test_tools_only_preflight_accepts_prerelease_archive_index(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            asset = root / "hosttools.tar.bz2"
            asset.write_bytes(b"host-tools-prerelease-test")
            checksum = hashlib.sha256(asset.read_bytes()).hexdigest()
            index = {
                "packages": [
                    {
                        "platforms": [
                            {
                                "version": "1.0.1-rc1",
                                "url": "https://invalid.example/platform.tar.bz2",
                                "archiveFileName": "platform.tar.bz2",
                                "checksum": "SHA-256:unused",
                                "size": "0",
                                "toolsDependencies": [
                                    {
                                        "name": build_release.HOST_TOOL_NAME,
                                        "version": build_release.HOST_TOOL_VERSION,
                                    }
                                ],
                            }
                        ],
                        "tools": [
                            {
                                "name": build_release.HOST_TOOL_NAME,
                                "version": build_release.HOST_TOOL_VERSION,
                                "systems": [
                                    {
                                        "host": "x86_64-pc-linux-gnu",
                                        "url": asset.as_uri(),
                                        "archiveFileName": asset.name,
                                        "checksum": f"SHA-256:{checksum}",
                                        "size": str(asset.stat().st_size),
                                    }
                                ],
                            }
                        ],
                    }
                ]
            }
            index_path = root / "archive-index.json"
            index_path.write_text(json.dumps(index), encoding="utf-8")
            completed = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "verify_public_release.py"),
                    "--index",
                    str(index_path),
                    "--version",
                    "1.0.1-rc1",
                    "--include-tools",
                    "--tools-only",
                    "--retries",
                    "1",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn("tool nrf54l15hosttools", completed.stdout)
            self.assertNotIn("platform 1.0.1-rc1 OK", completed.stdout)

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
