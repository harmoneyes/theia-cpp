#!/usr/bin/env python3
"""Drop a theia-native build into vendor/.

The compiled library and its two C headers are produced by theia-native's
release workflow and published to S3. This is the only supported way to refresh
them: there is no source in this repository.

    scripts/sync_vendor_bundle.py --version 1.4.2
    scripts/sync_vendor_bundle.py --from-dir ../theia-native/build-linux-c --platform linux-x86_64

The validation gates below exist because every one of them has a failure mode
that is silent at build time and wrong at run time.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VENDOR = REPO / "vendor"

S3_PREFIX = "s3://harmoneyes-internal-builds/standalone-sdk-builds"

# Our platform directory names, and what the release workflow calls them.
PLATFORMS = {
    "linux-x86_64": ("linux-x64", ["libtheia.so.0"]),
    "linux-aarch64": ("linux-arm64", ["libtheia.so.0"]),
    "macos-arm64": ("macos-arm64", ["libtheia.dylib"]),
    "windows-x86_64": ("windows-x64", ["theia.dll", "theia.lib"]),
}

HEADERS = ["theia_export.h", "theia_ace_c_api.h"]

# Minimum plausible size for a real library. A Git LFS pointer is ~130 bytes.
MIN_LIBRARY_BYTES = 4096

# Pinned in include/harmoneyes/theia/types.hpp, which aliases gaze_sample_t and
# static_asserts its size. A field added or reordered upstream changes what
# every column means while still compiling, so the count is checked, not just
# reported.
EXPECTED_GAZE_SAMPLE_FIELDS = 52


class Refused(SystemExit):
    def __init__(self, message: str) -> None:
        super().__init__(f"refusing to sync: {message}")


def _require_real_binary(path: Path) -> None:
    if not path.exists():
        raise Refused(f"{path} is missing")
    size = path.stat().st_size
    if size < MIN_LIBRARY_BYTES:
        raise Refused(
            f"{path} is {size} bytes, which is a Git LFS pointer or a stub, not a library"
        )


def _exports(library: Path) -> set[str]:
    """Exported symbol names, or an empty set if we cannot inspect this file."""
    for argv in (["nm", "-D", "--defined-only", str(library)],
                 ["llvm-nm", "--dynamic", "--defined-only", str(library)]):
        try:
            out = subprocess.run(argv, capture_output=True, text=True, check=True).stdout
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
        return {line.split()[-1] for line in out.splitlines() if line.strip()}
    return set()


def check_library(path: Path, platform: str) -> bool:
    """Validate one library. False means the export checks could not run."""
    _require_real_binary(path)

    symbols = _exports(path)
    if not symbols:
        # Typically a .dll/.lib/.dylib on a Linux box. Reported by the caller
        # rather than passed over silently, because the theia_neon_create gate
        # below is the whole point of this function.
        print(f"  {platform}: {path.name} size ok, but its symbols are unreadable here")
        return False

    if "theia_sdk_create" not in symbols:
        raise Refused(
            f"{path.name} does not export theia_sdk_create: it was built without "
            "THEIA_BUILD_C_API=ON and the C++ headers have nothing to call"
        )

    # compile.sh builds with THEIA_BUILD_PUPIL_LABS=OFF, which produces a Neon
    # tracker that silently never connects. Vendoring that would make Neon
    # appear supported and never work.
    if "theia_neon_create" not in symbols:
        raise Refused(
            f"{path.name} does not export theia_neon_create: it was built with "
            "THEIA_BUILD_PUPIL_LABS=OFF, so Pupil Labs Neon would silently never connect"
        )

    print(f"  {platform}: {path.name} ok "
          f"({len([s for s in symbols if s.startswith('theia_')])} theia_* exports)")
    return True


def check_header(header: Path) -> None:
    if not header.exists():
        raise Refused(f"{header} is missing")
    text = header.read_text(encoding="utf-8", errors="replace")

    # These two are hand-patched in theia-native so a zero-initialised options_t
    # keeps both models ON. Plain regeneration of the C API silently reverts
    # them to enable_*, which would turn our enable_*=true defaults into
    # DISABLED models with no diagnostic anywhere.
    if header.name == "theia_export.h":
        for field in ("disable_cog_load_model", "disable_fatigue_model"):
            if field not in text:
                raise Refused(
                    f"theia_export.h has no {field}: the inverted-boolean patch was lost "
                    "during regeneration, and a zeroed options_t would now disable that model"
                )

        # The C++ layer aliases gaze_sample_t and static_asserts its layout, so a
        # field reorder changes what every column means while still compiling.
        # [^}]* so the match starts at the NEAREST preceding brace, not the
        # first struct in the file.
        fields = re.search(r"typedef struct \{([^}]*)\} gaze_sample_t;", text, re.S)
        if not fields:
            raise Refused("theia_export.h has no gaze_sample_t")
        body = re.sub(r"/\*.*?\*/", "", fields.group(1), flags=re.S)
        body = re.sub(r"//[^\n]*", "", body)
        count = len(re.findall(r";", body))
        if count != EXPECTED_GAZE_SAMPLE_FIELDS:
            raise Refused(
                f"gaze_sample_t now has {count} fields, expected "
                f"{EXPECTED_GAZE_SAMPLE_FIELDS}. The C++ layer aliases this struct and "
                "asserts its layout, so a change here silently alters what every column "
                "means. Review the field order, update the static_asserts in "
                "include/harmoneyes/theia/types.hpp and EXPECTED_GAZE_SAMPLE_FIELDS here, "
                "then re-run."
            )
        print(f"  theia_export.h: gaze_sample_t has {count} fields, as expected")


def check_headers_identical(staged: dict[str, Path]) -> None:
    """The workflow publishes the same two headers beside every platform."""
    for name in HEADERS:
        digests = {}
        for platform, directory in staged.items():
            candidate = directory / name
            if candidate.exists():
                digests[platform] = hashlib.sha256(candidate.read_bytes()).hexdigest()
        if len(set(digests.values())) > 1:
            raise Refused(
                f"{name} differs between platforms ({digests}); the release jobs built "
                "from different sources"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--version", help="standalone-sdk version to pull from S3")
    source.add_argument("--from-dir", type=Path, help="a local build directory")
    parser.add_argument("--platform", choices=sorted(PLATFORMS),
                        help="required with --from-dir")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate without writing into vendor/")
    parser.add_argument("--allow-unvalidated", action="store_true",
                        help="proceed even when an artifact's exports cannot be read here")
    args = parser.parse_args()

    if args.from_dir and not args.platform:
        parser.error("--from-dir requires --platform")

    staging = VENDOR / ".staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    staged: dict[str, Path] = {}
    try:
        if args.version:
            for platform, (published, _libs) in PLATFORMS.items():
                destination = staging / platform
                destination.mkdir()
                url = f"{S3_PREFIX}/{args.version}/{published}/"
                print(f"fetching {url}")
                result = subprocess.run(["aws", "s3", "sync", url, str(destination)],
                                        capture_output=True, text=True)
                if result.returncode != 0:
                    raise Refused(
                        f"{published} could not be fetched ({result.stderr.strip()}). "
                        "Installing some platforms and stamping vendor/VERSION with the new "
                        "number would leave the rest silently stale."
                    )
                staged[platform] = destination
        else:
            destination = staging / args.platform
            destination.mkdir()
            for name in PLATFORMS[args.platform][1] + HEADERS:
                for candidate in (args.from_dir / name,
                                  args.from_dir / "c_api" / name,
                                  args.from_dir.parent / "c_api" / name):
                    if candidate.exists():
                        shutil.copy2(candidate, destination / name)
                        break
            # A versioned .so is published under its SONAME; a local build has
            # the real file under libtheia.so.0.1.0 with symlinks beside it.
            if args.platform.startswith("linux") and not (destination / "libtheia.so.0").exists():
                for candidate in sorted(args.from_dir.glob("libtheia.so.0.*")):
                    shutil.copy2(candidate, destination / "libtheia.so.0")
                    break
            staged[args.platform] = destination

        if not staged:
            raise Refused("nothing was fetched")

        print("\nvalidating:")
        unvalidated = []
        for platform, directory in staged.items():
            for library in PLATFORMS[platform][1]:
                if not check_library(directory / library, platform):
                    unvalidated.append(f"{platform}/{library}")
            for name in HEADERS:
                if not (directory / name).exists():
                    raise Refused(
                        f"{platform} has {name} missing. Installing a library beside a "
                        "stale header is precisely the mismatch that compiles cleanly and "
                        "misreads every field at run time."
                    )
                check_header(directory / name)
        check_headers_identical(staged)

        if unvalidated and not args.allow_unvalidated:
            raise Refused(
                "these artifacts could not have their exports checked on this machine: "
                + ", ".join(unvalidated)
                + ". The check that matters is theia_neon_create, whose absence means a "
                "THEIA_BUILD_PUPIL_LABS=OFF build where Neon silently never connects. "
                "Re-run on a machine with a reader for them, or pass --allow-unvalidated "
                "if you have confirmed them another way."
            )

        if args.dry_run:
            print("\ndry run: vendor/ untouched")
            return 0

        print("\ninstalling:")
        (VENDOR / "include" / "harmoneyes").mkdir(parents=True, exist_ok=True)
        for platform, directory in staged.items():
            target = VENDOR / platform
            target.mkdir(parents=True, exist_ok=True)
            for library in PLATFORMS[platform][1]:
                shutil.copy2(directory / library, target / library)
                print(f"  vendor/{platform}/{library}")
            for name in HEADERS:
                if (directory / name).exists():
                    shutil.copy2(directory / name, VENDOR / "include" / "harmoneyes" / name)

        if args.version:
            (VENDOR / "VERSION").write_text(args.version + "\n", encoding="utf-8")
            print(f"  vendor/VERSION -> {args.version}")
            print("\nremember to bump project(HarmonEyesNative VERSION ...) in CMakeLists.txt")
        return 0
    finally:
        shutil.rmtree(staging, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
