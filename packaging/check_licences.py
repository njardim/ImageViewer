#!/usr/bin/env python3
"""Licence gate for the vcpkg packages that end up in ImageViewer's packages.

usage: python3 packaging/check_licences.py <vcpkg_installed>/<triplet>

Reads share/<port>/vcpkg.spdx.json of every installed port (vcpkg writes the
licence of the port plus its enabled features to packages[0].licenseConcluded)
and fails when:
  - a port is under a GPL/AGPL licence with no LGPL or permissive alternative
    (an SPDX "OR" with an acceptable branch is fine, "AND" needs every part);
  - a port that must never ship is installed (FORBIDDEN, docs/PLAN.md §5).
Ports without a declared licence are listed as warnings. vcpkg's own build helpers
(vcpkg-cmake, vcpkg-make, ...), which CMakeLists.txt leaves out of the packages, are
listed as such when vcpkg's record of their files shows no binary (bin/, lib/). The crates
built into the SVG renderer (share/imageviewer-svg/crates.tsv, D-90) are checked one by one
under the same rules. Standard library only, so it runs unchanged on the Windows, macOS and
Linux runners.
"""
import json
import os
import re
import sys
from pathlib import Path

FORBIDDEN = {
    "x265": "HEVC encoder, GPL-2.0-or-later",
    "libde265": "HEVC decoder: not shipped, HEIC comes from the operating system (D-39)",
}
# GPL-2.0-only, GPL-3.0-or-later, legacy GPL-2.0+, AGPL-3.0-only...; LGPL-* does not match.
STRONG_COPYLEFT = re.compile(r"^A?GPL-", re.IGNORECASE)
UNKNOWN = {"NOASSERTION", "NONE", "LicenseRef-vcpkg-null"}
# Ports whose vcpkg port declares no licence, checked by hand against share/<port>/copyright.
# Pinned to the reviewed version: a new version is a warning again until re-checked.
REVIEWED = {}


def build_helper(installed, triplet, port):
    """True for a vcpkg-* port whose installed files (vcpkg/info/<port>_<version>_<triplet>.list)
    hold no library or program: scripts and data for the build, never shipped (vcpkg-make also
    installs two Automake scripts under GPL-2.0-or-later, run only while building)."""
    if not port.startswith("vcpkg-"):
        return False
    lists = list((installed / "vcpkg" / "info").glob(f"{port}_*_{triplet}.list"))
    if not lists:
        return False
    binaries = tuple(f"{triplet}/{d}/" for d in ("bin", "lib", "debug/bin", "debug/lib"))
    return not any(line.strip().startswith(binaries)
                   for path in lists for line in path.read_text(encoding="utf-8").splitlines())


class ExpressionError(ValueError):
    pass


def acceptable(expression):
    """True if the SPDX expression allows distribution under non-GPL terms. Cargo's older "A/B" means
    "A OR B"."""
    expression = re.sub(r"\s*/\s*", " OR ", expression)
    tokens = re.findall(r"\(|\)|[^\s()]+", expression)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def take():
        nonlocal pos
        if pos >= len(tokens):
            raise ExpressionError(f"unexpected end of '{expression}'")
        pos += 1
        return tokens[pos - 1]

    def operator(name):
        token = peek()
        return token is not None and token.upper() == name

    def primary():
        token = take()
        if token == "(":
            value = any_of()
            if take() != ")":
                raise ExpressionError(f"missing ')' in '{expression}'")
            return value
        if token == ")" or token.upper() in ("AND", "OR", "WITH"):
            raise ExpressionError(f"unexpected '{token}' in '{expression}'")
        value = not STRONG_COPYLEFT.match(token)
        if operator("WITH"):  # an exception does not turn a GPL licence into a permissive one
            take()
            take()
        return value

    def all_of():
        value = primary()
        while operator("AND"):
            take()
            value = primary() and value
        return value

    def any_of():
        value = all_of()
        while operator("OR"):
            take()
            value = all_of() or value
        return value

    result = any_of()
    if pos != len(tokens):
        raise ExpressionError(f"trailing '{' '.join(tokens[pos:])}' in '{expression}'")
    return result


def port_licence(spdx_path):
    """(licence or None, port version) from the port's SPDX document."""
    document = json.loads(spdx_path.read_text(encoding="utf-8"))
    package = document["packages"][0]  # SPDXRef-port
    version = package.get("versionInfo") or ""
    for field in ("licenseConcluded", "licenseDeclared"):
        value = (package.get(field) or "").strip()
        if value and value not in UNKNOWN:
            return value, version
    return None, version


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[2])
    tree = Path(sys.argv[1])
    share = tree / "share"
    annotate = os.environ.get("GITHUB_ACTIONS") == "true"
    errors, warnings, rows = [], [], []

    ports = sorted(p for p in share.iterdir() if p.is_dir()) if share.is_dir() else []
    if not ports:
        errors.append(f"no installed ports under {share}")
    for port_dir in ports:
        port = port_dir.name
        if port in FORBIDDEN:
            errors.append(f"{port} is installed but must not ship ({FORBIDDEN[port]})")
        spdx = port_dir / "vcpkg.spdx.json"
        if not spdx.is_file():
            if (port_dir / "copyright").is_file():  # every real port has one; skip CMake-only dirs
                warnings.append(f"{port}: no vcpkg.spdx.json, licence not checked")
            continue
        try:
            licence, version = port_licence(spdx)
        except (OSError, ValueError, KeyError, IndexError) as e:
            errors.append(f"{port}: unreadable {spdx.name}: {e}")
            continue
        if licence is None and port in REVIEWED and version.split("#")[0] == REVIEWED[port][0]:
            licence = REVIEWED[port][1]
        if licence is None and build_helper(tree.parent, tree.name, port):
            rows.append((port, "(build helper)", "not shipped"))
            continue
        if licence is None:
            rows.append((port, "(not declared)", "check by hand"))
            warnings.append(f"{port}: licence not declared in the port, check share/{port}/copyright by hand")
            continue
        try:
            ok = acceptable(licence)
        except ExpressionError as e:
            errors.append(f"{port}: cannot parse licence: {e}")
            continue
        rows.append((port, licence, "ok" if ok else "GPL, not allowed"))
        if not ok:
            errors.append(f"{port}: {licence} has no LGPL or permissive option")

    # The SVG renderer's crates (D-90): every one under the same rules as a port.
    port_count, crate_count = len(rows), 0
    crates = share / "imageviewer-svg" / "crates.tsv"
    if (share / "imageviewer-svg").is_dir() and not crates.is_file():
        errors.append("imageviewer-svg: no crates.tsv, the crates' licences are not checked")
    for line in crates.read_text(encoding="utf-8").splitlines() if crates.is_file() else []:
        name, version, licence, source = line.split("\t")
        label = f"imageviewer-svg: {name} {version}"
        if not licence:
            errors.append(f"{label} declares no licence")
            continue
        try:
            ok = acceptable(licence)
        except ExpressionError as e:
            errors.append(f"{label}: cannot parse licence: {e}")
            continue
        rows.append((f"  {name} {version}", licence, "ok" if ok else "GPL, not allowed"))
        crate_count += 1
        if not ok:
            errors.append(f"{label}: {licence} has no LGPL or permissive option")

    width = max((len(r[0]) for r in rows), default=4)
    for port, licence, verdict in rows:
        print(f"{port:<{width}}  {verdict:<16}  {licence}")
    for message in warnings:
        print(f"::warning::licence gate: {message}" if annotate else f"warning: {message}")
    for message in errors:
        print(f"::error::licence gate: {message}" if annotate else f"error: {message}")
    if errors:
        sys.exit(1)
    print(f"licence gate: {port_count} ports and {crate_count} crates checked, no GPL-only licence, "
          f"none of {', '.join(FORBIDDEN)}")


if __name__ == "__main__":
    main()
