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
import hashlib
import os
import re
import shutil
import subprocess
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


def verify_digest(archive: Path, expected: str) -> None:
    """Refuse an archive whose SHA-256 is not the one we were promised."""
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != expected.strip().lower():
        raise BundleError(f"sha256 mismatch: expected {expected}, got {digest}")
    log(f"sha256 ok ({digest[:16]}…)")


def materialise(bundle: str, workdir: Path, sha256: str | None) -> Path:
    """Return a directory holding the bundle, downloading/unpacking as needed."""
    if bundle.startswith("http://"):
        raise BundleError("refusing http://; a bundle must come over https")

    if bundle.startswith("https://"):
        # These binaries get shipped to customers, so the transport is not
        # allowed to be the only thing vouching for them.
        if not sha256:
            raise BundleError("--sha256 is required when fetching over the network")
        log(f"downloading {bundle}")
        archive = workdir / "bundle.tar.gz"
        with urllib.request.urlopen(bundle) as response, archive.open("wb") as out:
            shutil.copyfileobj(response, out)
        verify_digest(archive, sha256)
        return unpack(archive, workdir)

    path = Path(bundle).expanduser().resolve()
    if not path.exists():
        raise BundleError(f"no such bundle: {path}")
    if path.is_dir():
        if sha256:
            raise BundleError("--sha256 cannot be checked against a directory")
        return path
    if sha256:
        verify_digest(path, sha256)
    return unpack(path, workdir)


def unpack(archive: Path, workdir: Path) -> Path:
    """Unpack a tarball, refusing members that escape the destination."""
    target = workdir / "unpacked"
    target.mkdir()
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            destination = (target / member.name).resolve()
            # String prefixes let a sibling such as `unpacked2/` pass as being
            # inside `unpacked/`.
            if not destination.is_relative_to(target.resolve()):
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


def struct_bodies(header: Path) -> dict[str, list[str]]:
    """Raw field declarations per typedef'd struct, comments stripped."""
    source = header.read_text(encoding="utf-8", errors="replace")
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"//[^\n]*", "", source)

    bodies: dict[str, list[str]] = {}
    for match in re.finditer(r"typedef\s+struct\s*\{(.*?)\}\s*(\w+)\s*;", source, re.S):
        body, name = match.group(1), match.group(2)
        bodies[name] = [" ".join(f.split()) for f in body.split(";") if f.strip()]
    return bodies


def field_name(declaration: str) -> str:
    """The identifier a field declaration declares."""
    # `void (*cb)(void)` first, else the trailing identifier plus any extents.
    pointer = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", declaration)
    if pointer:
        return pointer.group(1)
    plain = re.search(r"(\w+)\s*(?:\[[^\]]*\])*\s*$", declaration)
    return plain.group(1) if plain else ""


def measure_layout(include_root: Path, bodies: dict[str, list[str]],
                   workdir: Path) -> dict[str, tuple[int, tuple[int, ...]]]:
    """sizeof and every field offsetof, as the compiler actually lays them out.

    Text comparison cannot do this job: redaction deliberately rewrites both
    the name and the type of a hidden field, so any check that tolerates
    redaction is blind to exactly the drift worth catching. The compiler is
    not.
    """
    lines = ['#include <stddef.h>', '#include <stdio.h>',
             '#include <harmoneyes/theia_export.h>', 'int main(void) {']
    wanted = [n for n in ABI_CRITICAL_STRUCTS if n in bodies]
    for name in wanted:
        lines.append(f'    printf("S {name} %zu\\n", sizeof({name}));')
        for declaration in bodies[name]:
            lines.append(f'    printf("F {name} %zu\\n", '
                         f'offsetof({name}, {field_name(declaration)}));')
    lines += ['    return 0;', '}']

    workdir.mkdir(parents=True, exist_ok=True)
    source = workdir / "layout_probe.c"
    binary = workdir / "layout_probe"
    source.write_text("\n".join(lines))

    compiler = os.environ.get("CC", "cc")
    build = subprocess.run(
        [compiler, "-I", str(include_root), "-o", str(binary), str(source)],
        capture_output=True, text=True)
    if build.returncode != 0:
        raise BundleError(
            f"could not compile the layout probe against {include_root}:\n"
            f"{build.stderr.strip()}")

    run = subprocess.run([str(binary)], capture_output=True, text=True)
    if run.returncode != 0:
        raise BundleError(f"the layout probe failed to run: {run.stderr.strip()}")

    sizes: dict[str, int] = {}
    offsets: dict[str, list[int]] = {}
    for line in run.stdout.splitlines():
        kind, name, value = line.split()
        if kind == "S":
            sizes[name] = int(value)
        else:
            offsets.setdefault(name, []).append(int(value))
    return {n: (sizes[n], tuple(offsets.get(n, ()))) for n in wanted}


def check_abi(bundle: Path, workdir: Path) -> list[str]:
    """Layout drift in the structs the C++ layer aliases or copies field by field.

    A bundle ships its header and its library together and the C++ is
    recompiled against both, so a field appended to the end of a struct is
    safe and must not block a release. What is never safe is an existing
    field moving, changing width, or disappearing.
    """
    current_root = VENDOR_DIR / "include"
    if not (current_root / "harmoneyes" / "theia_export.h").exists():
        return []

    new_root = bundle / "include"
    new_header = new_root / "harmoneyes" / "theia_export.h"
    current_header = current_root / "harmoneyes" / "theia_export.h"

    new_declarations = struct_bodies(new_header)
    current_declarations = struct_bodies(current_header)
    new = measure_layout(new_root, new_declarations, workdir / "new")
    current = measure_layout(current_root, current_declarations, workdir / "current")

    # Offsets prove where the bytes sit, but they cannot see a swap of two
    # same-width fields, nor a retype that padding absorbs (int -> int64_t
    # ahead of an 8-aligned field moves nothing). Declarations catch those.
    # Redacted fields are exempt, since hiding them is what rewrites their
    # text; offsets still cover them.
    problems = []
    for name, (current_size, current_offsets) in current.items():
        for index, (was, now) in enumerate(zip(current_declarations.get(name, []),
                                               new_declarations.get(name, []))):
            if was != now and "reserved_" not in was:
                problems.append(f"{name} field {index}: {was!r} -> {now!r}")

        if name not in new:
            problems.append(f"{name} is gone from the new header")
            continue
        new_size, new_offsets = new[name]
        if len(new_offsets) < len(current_offsets):
            problems.append(
                f"{name}: {len(current_offsets)} fields -> {len(new_offsets)}; "
                "a field was removed, which needs a major version bump")
            continue
        if new_size < current_size:
            problems.append(f"{name}: {current_size} bytes -> {new_size}")
            continue
        for index, (was, now) in enumerate(zip(current_offsets, new_offsets)):
            if was != now:
                problems.append(
                    f"{name} field {index} moved from offset {was} to {now}; "
                    "a layout change needs a major version bump")
    return problems


def validate(bundle: Path, workdir: Path) -> list[str]:
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
        problems.extend(check_abi(bundle, workdir))

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


def redact(declaration: str, index: int) -> str:
    """Rewrite one field declaration to hide its name, keeping its width.

    Pointers all measure the same, so an unresolvable typedef is safe as
    `const void*`. Primitives keep their own type, which is always spelled in
    terms of stdint.h. measure_layout re-checks the result either way.
    """
    if re.search(r"\(\s*\*\s*\w+\s*\)\s*\(", declaration):
        return f"void (*reserved_{index})(void)"
    if declaration.rstrip().endswith("*") or "*" in declaration:
        return f"const void* reserved_{index}"
    type_text = re.sub(r"\s*\w+\s*(?:\[[^\]]*\])*\s*$", "", declaration).strip()
    if type_text in {"char", "short", "int", "long", "float", "double", "size_t",
                     "int8_t", "int16_t", "int32_t", "int64_t",
                     "uint8_t", "uint16_t", "uint32_t", "uint64_t"}:
        return f"{type_text} reserved_{index}"
    # An unknown typedef by value: keep it and let the layout probe judge.
    return f"{type_text} reserved_{index}"


def extend_redaction(bundle: Path) -> list[str]:
    """Carry fields the engine appended into our redacted header.

    The vendored header hides internals the public SDK does not wrap, so it
    cannot simply be replaced by the bundle's. But a struct that grows
    upstream and not here is a struct the library writes past the end of, so
    the growth has to be mirrored. Appending `reserved_N` does that without
    disclosing anything.
    """
    current_header = VENDOR_DIR / "include" / "harmoneyes" / "theia_export.h"
    new_bodies = struct_bodies(bundle / "include" / "harmoneyes" / "theia_export.h")
    current_bodies = struct_bodies(current_header)

    source = current_header.read_text(encoding="utf-8")
    added = []
    for name in ABI_CRITICAL_STRUCTS:
        if name not in new_bodies or name not in current_bodies:
            continue
        extra = new_bodies[name][len(current_bodies[name]):]
        if not extra:
            continue
        taken = [int(n) for n in re.findall(r"reserved_(\d+)", " ".join(current_bodies[name]))]
        start = max(taken) + 1 if taken else 0
        fields = "".join(
            f"    {redact(declaration, start + offset)};\n"
            for offset, declaration in enumerate(extra))
        closing = re.search(r"\n\}\s*%s;" % re.escape(name), source)
        source = source[:closing.start() + 1] + fields + source[closing.start() + 1:]
        added.append(f"{name} +{len(extra)}")

    if added:
        current_header.write_text(source)
        log("extended the redacted header: " + ", ".join(added))
    return added


def sync(bundle: Path, workdir: Path) -> None:
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

    # vendor/include/ is this repo's own redacted header, not the bundle's.
    # Mirror any growth into it, then prove the result still matches.
    if extend_redaction(bundle):
        remaining = check_abi(bundle, workdir / "recheck")
        if remaining:
            raise BundleError(
                "the redacted header still does not match after extending it: "
                + "; ".join(remaining))
        log("redacted header re-checked against the bundle")

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
        "--sha256", help="expected SHA-256 of the archive; required for https bundles"
    )
    parser.add_argument(
        "--check-only", action="store_true", help="validate the bundle and write nothing"
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        try:
            bundle = materialise(args.bundle, Path(tmp), args.sha256)
            problems = validate(bundle, Path(tmp) / "abi")
        except BundleError as error:
            sys.exit(f"[sync_vendor_bundle] {error}")

        if problems:
            for problem in problems:
                log(f"REJECTED: {problem}")
            sys.exit(f"[sync_vendor_bundle] {len(problems)} problem(s); nothing was changed")

        log("bundle looks good")
        if args.check_only:
            return
        sync(bundle, Path(tmp) / "sync")
        log("done — review `git status` and commit vendor/")


if __name__ == "__main__":
    main()
