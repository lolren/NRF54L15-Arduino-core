#!/usr/bin/env python3
"""Generate CoreVersionGenerated.h from the package version string."""

import argparse
import re
from pathlib import Path


RELEASE_VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        required=True,
        help="Package version string, e.g. 1.0.1 or 1.0.1-rc1",
    )
    parser.add_argument("--out", required=True, type=Path, help="Output header path")
    return parser.parse_args()


def match_release_version(version):
    match = RELEASE_VERSION_RE.fullmatch(version)
    if not match:
        raise SystemExit(
            "Unsupported version format {!r}; expected "
            "major.minor.patch[-prerelease]".format(version)
        )
    prerelease = match.group(4)
    if prerelease is not None:
        for identifier in prerelease.split("."):
            if identifier.isdigit() and len(identifier) > 1 and identifier[0] == "0":
                raise SystemExit(
                    "Numeric prerelease identifiers must not contain leading "
                    "zeroes: {!r}".format(version)
                )
    return match


def parse_version(version):
    match = match_release_version(version)
    return tuple(int(match.group(index)) for index in (1, 2, 3))


def render_header(version, major, minor, patch):
    prerelease = match_release_version(version).group(4) or ""
    prerelease_flag = 1 if prerelease else 0
    return """#ifndef NRF54L15_CLEAN_CORE_VERSION_GENERATED_H
#define NRF54L15_CLEAN_CORE_VERSION_GENERATED_H

#define ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR {major}
#define ARDUINO_NRF54L15_CLEAN_VERSION_MINOR {minor}
#define ARDUINO_NRF54L15_CLEAN_VERSION_PATCH {patch}
#define ARDUINO_NRF54L15_CLEAN_VERSION_PRERELEASE "{prerelease}"
#define ARDUINO_NRF54L15_CLEAN_VERSION_IS_PRERELEASE {prerelease_flag}

#define ARDUINO_NRF54L15_CLEAN_VERSION_ENCODE(major, minor, patch) \\
    (((major) * 10000UL) + ((minor) * 100UL) + (patch))

#define ARDUINO_NRF54L15_CLEAN_VERSION \\
    ARDUINO_NRF54L15_CLEAN_VERSION_ENCODE( \\
        ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR, \\
        ARDUINO_NRF54L15_CLEAN_VERSION_MINOR, \\
        ARDUINO_NRF54L15_CLEAN_VERSION_PATCH)

#define ARDUINO_NRF54L15_CLEAN_VERSION_STRING "{version}"

#endif  // NRF54L15_CLEAN_CORE_VERSION_GENERATED_H
""".format(
        version=version,
        major=major,
        minor=minor,
        patch=patch,
        prerelease=prerelease,
        prerelease_flag=prerelease_flag,
    )


def main():
    args = parse_args()
    major, minor, patch = parse_version(args.version)
    # Path.resolve() on Python 3.5 raises FileNotFoundError when the target
    # file does not yet exist. Resolve only the parent (which must exist or be
    # created), then re-attach the filename. This is compatible with Python >= 3.4.
    parent = args.out.parent
    parent.mkdir(parents=True, exist_ok=True)
    output_path = parent.resolve() / args.out.name
    new_content = render_header(args.version, major, minor, patch)
    if output_path.exists():
        try:
            existing = output_path.read_text(encoding="utf-8")
            if existing == new_content:
                return 0
        except IOError:
            pass
    output_path.write_text(new_content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
