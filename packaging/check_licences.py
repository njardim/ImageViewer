#!/usr/bin/env python3
"""Licence gate for the vcpkg packages that end up in imageViewer's packages.

usage: python3 packaging/check_licences.py <vcpkg_installed>/<triplet>

Reads share/<port>/vcpkg.spdx.json of every installed port (vcpkg writes the
licence of the port plus its enabled features to packages[0].licenseConcluded)
and fails when:
  - a port is under a GPL/AGPL licence with no LGPL or permissive alternative
    (an SPDX "OR" with an acceptable branch is fine, "AND" needs every part);
  - a port that must never ship is installed (FORBIDDEN, docs/PLANO.md §5).
Ports without a declared licence are listed as warnings. Standard library only,
so it runs unchanged on the Windows, macOS and Linux runners.
"""
import json
import os
import re
import sys
from pathlib import Path

FORBIDDEN = {
    "x265": "HEVC encoder, GPL-2.0-or-later",
    "libde265": "HEVC decoder, held back until the patent question is cleared (D-P03)",
}
# GPL-2.0-only, GPL-3.0-or-later, legacy GPL-2.0+, AGPL-3.0-only...; LGPL-* does not match.
STRONG_COPYLEFT = re.compile(r"^A?GPL-", re.IGNORECASE)
UNKNOWN = {"NOASSERTION", "NONE", "LicenseRef-vcpkg-null"}


class ExpressionError(ValueError):
    pass


def acceptable(expression):
    """True if the SPDX expression allows distribution under non-GPL terms."""
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
    document = json.loads(spdx_path.read_text(encoding="utf-8"))
    package = document["packages"][0]  # SPDXRef-port
    for field in ("licenseConcluded", "licenseDeclared"):
        value = (package.get(field) or "").strip()
        if value and value not in UNKNOWN:
            return value
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[2])
    share = Path(sys.argv[1]) / "share"
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
            licence = port_licence(spdx)
        except (OSError, ValueError, KeyError, IndexError) as e:
            errors.append(f"{port}: unreadable {spdx.name}: {e}")
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

    width = max((len(r[0]) for r in rows), default=4)
    for port, licence, verdict in rows:
        print(f"{port:<{width}}  {verdict:<16}  {licence}")
    for message in warnings:
        print(f"::warning::licence gate: {message}" if annotate else f"warning: {message}")
    for message in errors:
        print(f"::error::licence gate: {message}" if annotate else f"error: {message}")
    if errors:
        sys.exit(1)
    print(f"licence gate: {len(rows)} ports checked, no GPL-only licence, none of {', '.join(FORBIDDEN)}")


if __name__ == "__main__":
    main()
