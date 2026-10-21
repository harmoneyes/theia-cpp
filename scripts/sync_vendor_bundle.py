#!/usr/bin/env python3
"""sync_vendor_bundle.py — drop a vendor bundle from theia-native into this repo.

The prebuilt Theia library is produced by theia-native's release CI and
published as a `.tar.gz` bundle. This script unpacks that bundle and replaces
the vendored trees here:

    vendor/<platform>/        the shared library per platform
    vendor/include/           the C declarations the C++ layer compiles against
    vendor/VERSION            the theia-native version the bundle was cut from

It is the only supported way to refresh the binaries — there is no engine
source in this repo.

Usage:
    # from a local bundle (directory or .tar.gz)
    python scripts/sync_vendor_bundle.py --bundle ../theia-native/dist_vendor
    python scripts/sync_vendor_bundle.py --bundle bundle.tar.gz

    # from a URL (CI downloads the artifact first; http(s) also works here)
    python scripts/sync_vendor_bundle.py --bundle https://…/vendor-bundle.tar.gz

    # check a bundle without writing anything
    python scripts/sync_vendor_bundle.py --bundle … --check-only

The validation gate runs before anything is replaced, so a bad bundle leaves
the repo untouched. See `--help` for the full list of checks.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VENDOR_DIR = REPO_ROOT / "vendor"

# Platform directory -> the library file that must be inside it. A bundle may
# carry any subset; whatever it carries has to be complete.
PLATFORM_LIBRARIES = {
    "linux-x86_64": "libtheia.so.0",
    "linux-aarch64": "libtheia.so.0",
    "macos-arm64": "libtheia.dylib",
    "windows-x86_64": "theia.dll",
}

# Windows cannot be linked without the import library, and CMake's
# IMPORTED_IMPLIB has nothing to point at if CI forgets to publish it.
WINDOWS_IMPORT_LIBRARY = "theia.lib"

# Structs the C++ layer aliases or copies field by field. A silent reorder here
# is the worst thing that can reach a customer: it compiles, links and reads
# the wrong bytes. sdk_impl.hpp static_asserts the sizes, but only against the
# header it was given, so the header itself has to be checked.
ABI_CRITICAL_STRUCTS = (
    "gaze_sample_t",
    "options_t",
    "config_t",
    "cog_load_result_t",
    "fatigue_result_t",
    "attention_result_t",
    "mental_readiness_result_t",
    "alert_event_t",
)


class BundleError(RuntimeError):
    """A bundle that must not be vendored."""


def log(message: str) -> None:
    print(f"[sync_vendor_bundle] {message}", file=sys.stderr)


# ── Fetching ──────────────────────────────────────────────────────────────


def materialise(bundle: str, workdir: Path) -> Path:
    """Return a directory holding the bundle, downloading/unpacking as needed."""
    if bundle.startswith(("http://", "https://")):
        log(f"downloading {bundle}")
        archive = workdir / "bundle.tar.gz"
        with urllib.request.urlopen(bundle) as response, archive.open("wb") as out:
            shutil.copyfileobj(response, out)
        return unpack(archive, workdir)

    path = Path(bundle).expanduser().resolve()
    if not path.exists():
        raise BundleError(f"no such bundle: {path}")
    if path.is_dir():
        return path
    return unpack(path, workdir)


def unpack(archive: Path, workdir: Path) -> Path:
    """Unpack a tarball, refusing members that escape the destination."""
    target = workdir / "unpacked"
    target.mkdir()
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            destination = (target / member.name).resolve()
            if not str(destination).startswith(str(target.resolve())):
                raise BundleError(f"tarball escapes its directory: {member.name}")
            if member.issym() or member.islnk():
                raise BundleError(f"tarball contains a link: {member.name}")
        tar.extractall(target)

    # Tolerate a bundle wrapped in one top-level directory.
    entries = list(target.iterdir())
    if len(entries) == 1 and entries[0].is_dir():
        return entries[0]
    return target


# ── Validation ────────────────────────────────────────────────────────────


def parse_structs(header: Path) -> dict[str, list[str]]:
    """Field lists per typedef'd struct, comments and spacing normalised away."""
    source = header.read_text(encoding="utf-8", errors="replace")
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)

    structs: dict[str, list[str]] = {}
    for match in re.finditer(r"typedef\s+struct\s*\{(.*?)\}\s*(\w+)\s*;", source, re.S):
        body, name = match.group(1), match.group(2)
        structs[name] = [" ".join(f.split()) for f in body.split(";") if f.strip()]
    return structs


def check_abi(new_header: Path, current_header: Path) -> list[str]:
    """Field-order drift in the structs the C++ layer depends on."""
    if not current_header.exists():
        return []

    new, current = parse_structs(new_header), parse_structs(current_header)
    problems = []
    for name in ABI_CRITICAL_STRUCTS:
        if name not in current:
            continue
        if name not in new:
            problems.append(f"{name} is gone from the new header")
            continue
        if len(new[name]) != len(current[name]):
            problems.append(
                f"{name}: {len(current[name])} fields -> {len(new[name])}; "
                "a layout change needs a major version bump"
            )
            continue
        for index, (was, now) in enumerate(zip(current[name], new[name])):
            # A field renamed to reserved_N is deliberate redaction, not drift:
            # the slot keeps its width, so the layout is unchanged.
            if was != now and "reserved_" not in now and "reserved_" not in was:
                problems.append(f"{name} field {index}: {was!r} -> {now!r}")
    return problems


def validate(bundle: Path) -> list[str]:
    """Every reason this bundle must not be vendored. Empty means good."""
    problems = []

    version_file = bundle / "VERSION"
    if not version_file.exists():
        problems.append("VERSION is missing")
    else:
        version = version_file.read_text().strip()
        if not version:
            problems.append("VERSION is empty")
        elif "local" in version or "dirty" in version:
            problems.append(f"VERSION {version!r} is a local build, not a release")

    header = bundle / "include" / "harmoneyes" / "theia_export.h"
    if not header.exists():
        problems.append("include/harmoneyes/theia_export.h is missing")
    else:
        problems.extend(check_abi(header, VENDOR_DIR / "include" / "harmoneyes" / "theia_export.h"))

    platforms = [d for d in bundle.iterdir() if d.is_dir() and d.name in PLATFORM_LIBRARIES]
    if not platforms:
        problems.append("the bundle carries no recognised platform directory")

    for platform in platforms:
        library = platform / PLATFORM_LIBRARIES[platform.name]
        if not library.exists():
            problems.append(f"{platform.name}/ has no {PLATFORM_LIBRARIES[platform.name]}")
            continue
        if library.stat().st_size == 0:
            problems.append(f"{platform.name}/{library.name} is empty")
            continue

        blob = library.read_bytes()
        if b"-----BEGIN PRIVATE KEY-----" in blob or b"-----BEGIN RSA PRIVATE KEY-----" in blob:
            problems.append(f"{platform.name}/{library.name} contains a private key")
        if b"theia_default_dev_key_2026" in blob:
            problems.append(
                f"{platform.name}/{library.name} was built with the dev model key, "
                "so it cannot decrypt released models"
            )
        if b"requires Options::lease_token" not in blob:
            problems.append(
                f"{platform.name}/{library.name} was built without THEIA_REQUIRE_LEASE, "
                "so it runs with no lease at all"
            )

        # Windows links against the import library, not the DLL.
        if platform.name.startswith("windows") and not (platform / WINDOWS_IMPORT_LIBRARY).exists():
            problems.append(f"{platform.name}/ has no {WINDOWS_IMPORT_LIBRARY}")

    return problems


# ── Replacing ─────────────────────────────────────────────────────────────


def sync(bundle: Path) -> None:
    VENDOR_DIR.mkdir(parents=True, exist_ok=True)

    for name in PLATFORM_LIBRARIES:
        source = bundle / name
        if not source.is_dir():
            continue
        destination = VENDOR_DIR / name
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
        log(f"vendored {name}/")

    include_source = bundle / "include"
    if include_source.is_dir():
        include_destination = VENDOR_DIR / "include"
        if include_destination.exists():
            shutil.rmtree(include_destination)
        shutil.copytree(include_source, include_destination)
        log("vendored include/")

    version = (bundle / "VERSION").read_text().strip()
    (VENDOR_DIR / "VERSION").write_text(version + "\n")
    log(f"vendored VERSION {version}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--bundle", required=True, help="bundle directory, .tar.gz, or http(s) URL"
    )
    parser.add_argument(
        "--check-only", action="store_true", help="validate the bundle and write nothing"
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        try:
            bundle = materialise(args.bundle, Path(tmp))
            problems = validate(bundle)
        except BundleError as error:
            sys.exit(f"[sync_vendor_bundle] {error}")

        if problems:
            for problem in problems:
                log(f"REJECTED: {problem}")
            sys.exit(f"[sync_vendor_bundle] {len(problems)} problem(s); nothing was changed")

        log("bundle looks good")
        if args.check_only:
            return
        sync(bundle)
        log("done — review `git status` and commit vendor/")


if __name__ == "__main__":
    main()
