#!/usr/bin/env python3
"""Checks every pinned dependency against its upstream releases and, with --apply, moves it to
the newest release it may take (D-46, D-62, D-67). Run by .github/workflows/dependency-update.yml
when an issue gets the "auto update" label; it also runs locally (without --apply it only reports).

What is pinned, and where:
- overlay ports in packaging/vcpkg/ports (OpenImageIO, libheif, libaom, expat, minizip-ng, and
  OpenJPH, LibRaw and OpenJPEG for the patches we carry: a git tag's commit; GraphicsMagick: a
  release archive and its SHA-512);
- every other vcpkg port through the manifest's builtin-baseline (a vcpkg release tag's commit);
- Qt in packaging/qt-version.txt (read by .github/workflows/build.yml, whose files the workflow's
  token may not change) and packaging/licenses/Qt.txt;
- the SVG renderer (D-90, packaging/vcpkg/ports/imageviewer-svg): resvg as an overlay port (a tag's
  commit), the crate's version and Cargo.lock with it (the lock regenerated from the new tag's own
  Cargo.lock and checked to hold nothing else), and the Rust that builds it (rust-toolchain.toml).

Policy:
- Libraries move within their major version; Qt only to the latest patch release of its minor
  version (the viewer uses Qt's private QRhi API, D-09). Anything newer is left for a person.
- Cooldown (D-67): a release is taken only once it is MIN_RELEASE_AGE_DAYS old, so that a
  compromised or broken release is found and withdrawn upstream before it reaches us. The age is
  GitHub's release date where there is one, the upload date of a release archive, or else the tag's
  own date. A vcpkg baseline is as young as the youngest library it moves. The number of days is
  packaging/cooldown-days.txt (15; lowered by the owner to take the latest releases sooner, 0 for
  every latest release).
- Security exception (D-67): when the pinned version has a known vulnerability (OSV) and no release
  old enough fixes it, the oldest younger release that does is taken, and marked for a person to
  review its changes by hand before merging; it is recorded in packaging/cooldown-exceptions.txt,
  and removed from there once it is past the cooldown or no longer pinned (D-73). A vulnerability
  a patch of ours fixes, or one assessed as out of reach (packaging/known-vulnerabilities.txt),
  needs no exception.
- A release archive is taken only when it matches its tag file for file (scripts/audit_dependencies.py).
- Every move lists what changed upstream (build system, scripts, CI, binary files) for review.
- An overlay port's own patches are tried on the new source in order: a patch upstream already
  contains is removed (D-57), one that no longer applies is reported (the build will fail on it),
  and for one of ours (imageviewer-*.patch, docs/PLAN.md section 15) the report says when its
  "Upstream:" fix may have landed or OSV no longer finds what its "Fixes:" line names.

usage: python3 scripts/update_dependencies.py [--apply] [--report FILE]
Writes changed=true|false, failed=true|false, exception=true|false and cooldown_days=<days> to
$GITHUB_OUTPUT when it is set.
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audit_dependencies as audit  # noqa: E402

# Shared with the audit (D-67): the paths, the cooldown, the overlay ports fetched by git
# (audit.COMMIT_PORTS: repository and tag template), reading and fetching.
ROOT, PORTS, MIN_RELEASE_AGE_DAYS = audit.ROOT, audit.PORTS, audit.MIN_RELEASE_AGE_DAYS
read, fetch = audit.read, audit.fetch
GM_ARCHIVE = "https://downloads.sourceforge.net/project/graphicsmagick/graphicsmagick/{0}/GraphicsMagick-{0}.tar.xz"
VCPKG = audit.VCPKG

# What a reviewer reads first in an upstream change (the xz backdoor of 2024 came in through
# build scripts and binary test files).
SENSITIVE = [
    ("build system or scripts", re.compile(r"(^|/)(CMakeLists\.txt|[^/]+\.cmake|configure(\.ac|\.in)?|[^/]+\.m4|"
                                           r"Makefile[^/]*|meson\.build|meson_options\.txt|[^/]+\.(sh|py|pl|bat|ps1))$")),
    ("CI", re.compile(r"(^|/)\.(github|gitlab-ci\.yml|travis\.yml|circleci|cirrus\.yml|appveyor\.yml)")),
    ("binary or compressed", re.compile(r"\.(a|o|so|dll|dylib|exe|lib|obj|jar|class|pyc|bin|xz|lzma|gz|bz2|zst|zip|7z)$")),
]


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def git(*args, cwd=None):
    return audit.run("git", *args, cwd=cwd)


def version_tuple(text):
    return tuple(int(part) for part in text.split("."))


def as_text(version):
    return ".".join(str(part) for part in version)


tag_date, github_repository, age_days = audit.tag_date, audit.github_repository, audit.age_days


def ripe(when):
    return age_days(when) >= MIN_RELEASE_AGE_DAYS


def answered(port):
    """The vulnerability ids of a library that need no new release: those a patch of ours fixes
    ("Fixes:") and those a person assessed as out of the packages' reach (D-73). An accepted risk
    is not among them: a release that fixes it is still taken as soon as it may be."""
    ids = {vid for patch in audit.our_patches(port) for vid in patch["fixes"]}
    return ids | {vid for (library, vid), text in audit.KNOWN_VULNERABILITIES.items()
                  if library == port and text.startswith("out of reach")}


def fixable(commit, port=None):
    """The ids of the known vulnerabilities of a commit that have a fix upstream (OSV), less those
    the port answers (answered())."""
    if not commit:
        return set()
    skip = answered(port) if port else set()
    return {vid for vid, aliases, _, fixed in audit.osv_lookup([commit]).get(commit, [])
            if fixed and not ({vid, *(a.strip() for a in aliases.split(",") if a.strip())} & skip)}


class Report:
    def __init__(self):
        self.rows, self.notes, self.review = [], [], []
        self.changed = self.exception = False
        self.exceptions = {}  # name: (version, the vulnerabilities its release fixes)

    def row(self, name, pinned, latest, action):
        self.rows.append(f"| {name} | {pinned} | {latest} | {action} |")

    def markdown(self):
        lines = ["| Dependency | Pinned | Latest stable | Action |", "|---|---|---|---|", *self.rows]
        if self.notes:
            lines += ["", *[f"- {note}" for note in self.notes]]
        if self.review:
            lines += ["", "### Upstream changes to review before merging (D-67)", "",
                      "Read at least the files flagged below; a change to the build system, the scripts or "
                      "the CI, or a new binary file, needs a reason in the release notes.", "", *self.review]
        return "\n".join(lines) + "\n"


def choose(name, current, candidates, released, vulnerable_now, fixes, report):
    """The version to move to, or None. `candidates`: the versions newer than `current` that the
    policy allows, ascending; released(v) -> (datetime, how known); vulnerable_now: the ids of the
    pinned version's fixable vulnerabilities; fixes(v) -> True when v has none of them.

    The newest candidate at least MIN_RELEASE_AGE_DAYS old; when that one (or the pinned version,
    if none is old enough) is still vulnerable, the oldest younger candidate that is not, as a
    security exception for a person to review."""
    held = []
    target = None
    for version in reversed(candidates):
        when, how = released(version)
        if ripe(when):
            target = version
            break
        held.append((version, when, how))
    if held:
        version, when, how = held[0]
        until = (when + datetime.timedelta(days=MIN_RELEASE_AGE_DAYS)).date().isoformat()
        report.notes.append(f"{name} {as_text(version)}: released {age_days(when):.0f} days ago ({how}); held by the "
                            f"{MIN_RELEASE_AGE_DAYS}-day cooldown until {until} (D-67).")
    if vulnerable_now and (target is None or not fixes(target)):
        for version, when, how in reversed(held):  # oldest first
            if fixes(version):
                report.exception = True
                report.exceptions[name] = (version, sorted(vulnerable_now))
                report.notes.append(f"**{name} {as_text(version)}: security exception (D-67)**: the pinned version has "
                                    f"{', '.join(sorted(vulnerable_now))} (OSV), fixed in this release, which is only "
                                    f"{age_days(when):.0f} days old ({how}). Review its upstream changes by hand "
                                    f"before merging.")
                return version
        report.notes.append(f"**{name}: the pinned version has {', '.join(sorted(vulnerable_now))} (OSV) and no "
                            f"release allowed here fixes it**: a person decides (a new major version, a patch "
                            f"in an overlay port, or a newer vcpkg port).")
    return target


def record_exception(entries, reason):
    """Adds "library version" entries to packaging/cooldown-exceptions.txt, so that the release gate
    lets them through (D-67, D-73); a person reviews them in the pull request. Data, never code:
    the reason carries OSV's vulnerability ids, text from outside, kept to one printable line."""
    reason = re.sub(r"[^\x20-\x7e]+", " ", reason).strip()
    lines = "".join(f"{entry}: {reason} ({audit.datetime.date.today().isoformat()})\n" for entry in entries
                    if entry not in audit.COOLDOWN_EXCEPTIONS)
    write(audit.EXCEPTIONS_FILE, read(audit.EXCEPTIONS_FILE) + lines)


def prune_exceptions(apply, report):
    """Removes the exceptions whose release is past the cooldown or no longer pinned (D-73), as the
    audit finds them with the pins as they are now: none outlives its release."""
    check = audit.Report()
    baseline_commit = json.loads(read(os.path.join(ROOT, "vcpkg.json")))["builtin-baseline"]
    audit.cooldown(baseline_commit, audit.baseline_versions(baseline_commit), check)
    if not check.stale:
        return
    report.notes.append("Expired cooldown exceptions, removed from packaging/cooldown-exceptions.txt: "
                        + ", ".join(f"`{key}`" for key in check.stale) + ".")
    report.changed = True  # a pull request, even when no pin moved
    if apply:
        keep = [line for line in read(audit.EXCEPTIONS_FILE).splitlines(keepends=True)
                if line.lstrip().startswith("#") or line.partition(": ")[0].strip() not in check.stale]
        write(audit.EXCEPTIONS_FILE, "".join(keep))


def review_changes(name, url, old_tag, new_tag, report):
    """What changed upstream between two tags, flagged for review (files only: trees, no blobs)."""
    with tempfile.TemporaryDirectory() as work:
        git("init", "-q", work)
        git("fetch", "-q", "--depth", "1", "--filter=blob:none", url,
            f"+refs/tags/{old_tag}:refs/tags/old", f"+refs/tags/{new_tag}:refs/tags/new", cwd=work)
        changes = [line.split("\t") for line in git("diff", "--no-renames", "--name-status", "old", "new",
                                                    cwd=work).splitlines()]
    added = sum(1 for c in changes if c[0] == "A")
    deleted = sum(1 for c in changes if c[0] == "D")
    lines = [f"- **{name}** `{old_tag}` → `{new_tag}`: {len(changes)} files changed ({added} added, {deleted} deleted)"]
    if repository := github_repository(url):
        lines[0] += f"; https://github.com/{repository}/compare/{old_tag}...{new_tag}"
    for label, pattern in SENSITIVE:
        hits = [f"{status} `{path}`" for status, path in changes if pattern.search(path)]
        if hits:
            lines.append(f"  - {label} ({len(hits)}): " + ", ".join(hits[:15]) + (" …" if len(hits) > 15 else ""))
    report.review += lines


def check_patches(port, url, tag, apply, report):
    """Tries the port's patches, in the portfile's order, on the new source."""
    portfile = os.path.join(PORTS, port, "portfile.cmake")
    text = read(portfile)
    patches = [p for p in re.findall(r"([\w.-]+\.(?:patch|diff))\b", text) if os.path.exists(os.path.join(PORTS, port, p))]
    if not patches:
        return
    ours = {patch["file"]: patch for patch in audit.our_patches(port)}
    with tempfile.TemporaryDirectory() as source:
        git("clone", "--quiet", "--depth", "1", "--branch", tag, url, source)
        commit = git("rev-parse", "HEAD", cwd=source).strip()
        still = {vid for vid, aliases, _, _ in audit.osv_lookup([commit]).get(commit, [])
                 for vid in (vid, *(a.strip() for a in aliases.split(",") if a.strip()))}
        for patch in dict.fromkeys(patches):
            path = os.path.join(PORTS, port, patch)
            options = ["--ignore-whitespace", "--whitespace=nowarn"]
            if patch in ours and ours[patch]["fixes"] and not (ours[patch]["fixes"] & still):
                report.notes.append(f"{port}: OSV finds none of {', '.join(sorted(ours[patch]['fixes']))} in {tag}: "
                                    f"check that upstream fixed it there and remove `{patch}` (D-57).")
            if subprocess.run(["git", "apply", "--check", *options, path], cwd=source, capture_output=True).returncode == 0:
                subprocess.run(["git", "apply", *options, path], cwd=source, check=True, capture_output=True)
                # Ours, with an upstream fix named in its header: that fix may differ from ours and
                # still leave our patch applicable.
                if patch in ours and not ours[patch]["upstream"].startswith("none"):
                    report.notes.append(f"{port}: `{patch}` still applies to {tag}; check whether its upstream fix "
                                        f"({ours[patch]['upstream']}) is in {tag} and remove the patch if it is (D-57).")
            elif subprocess.run(["git", "apply", "--check", "--reverse", *options, path], cwd=source,
                                capture_output=True).returncode == 0:
                report.notes.append(f"{port}: `{patch}` is already in {tag}; removed (D-57).")
                if apply:
                    os.remove(path)
                    text = "\n".join(line for line in text.split("\n") if not re.search(rf"\b{re.escape(patch)}\b", line))
            else:
                report.notes.append(f"{port}: `{patch}` no longer applies to {tag}: rebase it, or remove it if "
                                    f"upstream fixed the defect another way. The build fails until then.")
    if apply:
        write(portfile, text)


def update_git_port(port, url, templates, apply, report):
    manifest = os.path.join(PORTS, port, "vcpkg.json")
    pinned = json.loads(read(manifest))["version"]
    current = version_tuple(pinned)
    tags = audit.release_tags(url, templates, port)
    overall = max(tags) if tags else current
    held = f" ({as_text(overall)} is a new major version: for a person)" if overall[:1] > current[:1] else ""
    candidates = sorted(v for v in tags if v > current and v[:1] == current[:1])
    old_commit, old_tag = re.search(r"REF ([0-9a-f]{40}) # (\S+)", read(os.path.join(PORTS, port, "portfile.cmake"))).groups()
    vulnerable_now = fixable(old_commit, port)
    target = choose(port, current, candidates, lambda v: tag_date(url, tags[v][0]), vulnerable_now,
                    lambda v: not (fixable(tags[v][1]) & vulnerable_now), report)
    if target is None:
        report.row(port, pinned, as_text(overall), ("held (cooldown)" if candidates else "up to date") + held)
        return
    new = as_text(target)
    tag, commit = tags[target]
    report.row(port, pinned, as_text(overall), f"**→ {new}**" + held)
    report.changed = True
    review_changes(port, url, old_tag, tag, report)
    if apply:
        portfile = os.path.join(PORTS, port, "portfile.cmake")
        text = re.sub(r"REF [0-9a-f]{40} # \S+", f"REF {commit} # {tag}", read(portfile))
        # FETCH_REF names the tag where it does not use ${VERSION} (expat); "Overlay" comments name the version.
        text = "\n".join(line.replace(old_tag, tag) if "FETCH_REF" in line
                         else line.replace(pinned, new) if "Overlay" in line else line for line in text.split("\n"))
        write(portfile, text)
        write(manifest, read(manifest).replace(f'"version": "{pinned}"', f'"version": "{new}"'))
        update_manifest_comment(port, pinned, new)
        if port in report.exceptions:
            record_exception([f"{port} {new}"], "security: fixes " + ", ".join(report.exceptions[port][1]))
        if port == "imageviewer-svg":
            update_svg_crate(pinned, new, url, tag, report)
    check_patches(port, url, tag, apply, report)


def update_svg_crate(old, new, url, tag, report):
    """The SVG renderer's crate after resvg moved from `old` to `new` (D-90): its version and the pin on
    resvg in Cargo.toml, and Cargo.lock regenerated from the tag's own Cargo.lock (cargo keeps the tag's
    versions and leaves out what the crate does not use), then checked to hold no crate, version or
    checksum the tag's lock does not. Whether lib.rs still compiles against the new resvg is the
    update's own CI build: a release whose API changed fails there, and main keeps the pinned one."""
    import shutil
    import tomllib  # Python 3.11
    port = os.path.join(PORTS, "imageviewer-svg")
    manifest = os.path.join(port, "Cargo.toml")
    write(manifest, read(manifest).replace(f'version = "{old}"', f'version = "{new}"')
                                  .replace(f'version = "={old}"', f'version = "={new}"'))
    with tempfile.TemporaryDirectory() as work:
        source = os.path.join(work, "resvg")
        git("clone", "--quiet", "--depth", "1", "--branch", tag, url, source)
        crate = os.path.join(source, "imageviewer-svg")
        os.makedirs(crate)
        for name in ("Cargo.toml", "lib.rs"):  # not rust-toolchain.toml: any recent cargo resolves the same
            shutil.copy(os.path.join(port, name), crate)
        shutil.copy(os.path.join(source, "Cargo.lock"), os.path.join(crate, "Cargo.lock"))
        audit.run("cargo", "metadata", "--format-version", "1", cwd=crate)  # prunes the lock to this crate
        with open(os.path.join(source, "Cargo.lock"), "rb") as f:
            theirs = {(p["name"], p["version"]): p.get("checksum") for p in tomllib.load(f)["package"]}
        with open(os.path.join(crate, "Cargo.lock"), "rb") as f:
            ours = tomllib.load(f)["package"]
        foreign = [f"{p['name']} {p['version']}" for p in ours
                   if "source" in p and theirs.get((p["name"], p["version"]), "missing") != p.get("checksum")]
        if foreign:
            raise RuntimeError(f"imageviewer-svg: the regenerated Cargo.lock holds crates the {tag} lock does not: "
                               + ", ".join(foreign))
        shutil.copy(os.path.join(crate, "Cargo.lock"), os.path.join(port, "Cargo.lock"))
        needed = re.search(r'^rust-version = "([\d.]+)"', read(os.path.join(source, "crates", "resvg", "Cargo.toml")), re.M)
        if needed and version_tuple(needed.group(1)) > version_tuple(audit.rust_version()):
            report.notes.append(f"resvg {new} needs Rust {needed.group(1)}, newer than the pinned "
                                f"{audit.rust_version()}: the build fails until the toolchain moves.")
    report.review.append(f"- **imageviewer-svg**: Cargo.toml and Cargo.lock follow resvg {new} (only the crates of its "
                         f"own lock); lib.rs uses the resvg API listed at its top: read the changelog for those.")


def update_rust(apply, report):
    """The Rust release that builds the SVG renderer (D-90): the newest stable release past the
    cooldown. Its standard library is compiled into the program."""
    pin = os.path.join(PORTS, "imageviewer-svg", "rust-toolchain.toml")
    pinned = audit.rust_version()
    current = version_tuple(pinned)
    tags = {v: tag for v, tag in audit.release_tags(audit.RUST, "{v}").items() if len(v) == 3 and v[0] == 1}
    overall = max(tags) if tags else current
    candidates = sorted(v for v in tags if v > current)
    vulnerable_now = fixable(tags.get(current, (None, None))[1])
    target = choose("Rust", current, candidates, lambda v: tag_date(audit.RUST, tags[v][0]), vulnerable_now,
                    lambda v: not (fixable(tags[v][1]) & vulnerable_now), report)
    if target is None:
        report.row("Rust", pinned, as_text(overall), "held (cooldown)" if candidates else "up to date")
        return
    new = as_text(target)
    report.row("Rust", pinned, as_text(overall), f"**→ {new}**")
    report.review.append(f"- **Rust** {pinned} → {new}: read the release notes "
                         f"(https://github.com/rust-lang/rust/blob/{new}/RELEASES.md) for changes to the standard library.")
    report.changed = True
    if apply:
        write(pin, read(pin).replace(f'channel = "{pinned}"', f'channel = "{new}"'))
        if "Rust" in report.exceptions:
            record_exception([f"rust {new}"], "security: fixes " + ", ".join(report.exceptions["Rust"][1]))


def update_manifest_comment(port, old, new):
    """The root manifest's comment on an overlay port names its version."""
    path = os.path.join(ROOT, "vcpkg.json")
    lines = read(path).split("\n")
    write(path, "\n".join(line.replace(old, new) if f"packaging/vcpkg/ports/{port}" in line else line for line in lines))


def update_tarball_port(port, releases, archive, apply, report):
    """An overlay port built from a release tarball: the newest release (from `releases`, version
    to upload date) the policy allows. The new archive is pinned only when it matches its tag file
    for file (audit_dependencies.compare_tarball); its SHA-512 is computed here."""
    manifest = os.path.join(PORTS, port, "vcpkg.json")
    data = json.loads(read(manifest))
    key = "version-semver" if "version-semver" in data else "version"
    pinned = data[key]
    current = version_tuple(pinned)
    kind, repository, template = audit.TARBALL_PORTS[port]
    versions = {version_tuple(v): when for v, when in releases().items()}
    overall = max(versions) if versions else current
    candidates = sorted(v for v in versions if v > current and v[:1] == current[:1])

    def commit_of(version):
        return audit.release_tag(repository, template, as_text(version))[1] if kind == "git" else None
    vulnerable_now = fixable(commit_of(current), port)
    target = choose(port, current, candidates, lambda v: (versions[v], "archive upload date"), vulnerable_now,
                    lambda v: not (fixable(commit_of(v)) & vulnerable_now), report)
    if kind != "git":
        report.notes.append(f"{port}: OSV has no commit index for its Mercurial repository; read its NEWS for "
                            f"security fixes ({repository}).")
    if target is None:
        report.row(port, pinned, as_text(overall), "held (cooldown)" if candidates else "up to date")
        return
    new = as_text(target)
    url = archive.format(new)
    with tempfile.TemporaryDirectory() as work:
        path = os.path.join(work, "archive")
        with open(path, "wb") as f:
            f.write(fetch(url))
        sha512 = audit.sha512_of(path)
    check = audit.Report()
    tag = audit.release_tag(repository, template, new)[0] if kind == "git" else template.format(v_=new.replace(".", "_"))
    if not audit.compare_tarball(port, new, url, sha512, kind, repository, tag, check):
        report.row(port, pinned, as_text(overall), f"**{new} refused: its archive does not match `{tag}`**")
        report.notes += check.notes
        return
    report.row(port, pinned, as_text(overall), f"**→ {new}** (archive identical to `{tag}`)")
    report.changed = True
    if kind == "git":
        old_tag = audit.release_tag(repository, template, pinned)[0]
        review_changes(port, repository, old_tag, tag, report)
    else:
        report.review.append(f"- **{port}** {pinned} → {new}: read the NEWS of {new} ({repository}); the archive "
                             f"matches `{tag}` file for file.")
    if apply:
        portfile = os.path.join(PORTS, port, "portfile.cmake")
        write(portfile, re.sub(r"SHA512 [0-9a-f]{128}", f"SHA512 {sha512}", read(portfile)))
        write(manifest, read(manifest).replace(f'"{key}": "{pinned}"', f'"{key}": "{new}"'))
        update_manifest_comment(port, pinned, new)
        if port in report.exceptions:
            record_exception([f"{port} {new}"], "security: fixes " + ", ".join(report.exceptions[port][1]))


def update_vcpkg_baseline(apply, report):
    """The newest vcpkg release newer than the baseline whose own tag and every library it moves
    (of those the packages ship) are past the cooldown."""
    path = os.path.join(ROOT, "vcpkg.json")
    baseline = json.loads(read(path))["builtin-baseline"]
    tags = audit.release_tags(VCPKG, "{v}")
    # The baseline may be a commit between two releases: only releases it is an ancestor of are
    # newer. Asked of git (commits only, no trees or files) rather than GitHub's API.
    recent = sorted(tags)[-6:]
    with tempfile.TemporaryDirectory() as work:
        git("init", "-q", work)
        git("fetch", "-q", "--filter=tree:0", VCPKG, baseline, *(tags[v][1] for v in recent), cwd=work)
        candidates = [v for v in recent if tags[v][1] != baseline and subprocess.run(
            ["git", "merge-base", "--is-ancestor", baseline, tags[v][1]], cwd=work).returncode == 0]
    latest = tags[max(tags)][0]
    if not candidates:
        report.row("vcpkg baseline (every other port)", baseline[:10], latest, "up to date")
        return
    pinned = audit.baseline_versions(baseline)
    shipped = audit.BASELINE_PORTS
    moves = {}

    def moved(version):
        if version not in moves:
            new = audit.baseline_versions(tags[version][1])
            moves[version] = {p: (pinned.get(p), new[p]) for p in shipped if p in new and new[p] != pinned.get(p)}
        return moves[version]

    def released(version):
        when, how = tag_date(VCPKG, tags[version][0])
        for port, (_, new) in moved(version).items():
            url, template = shipped[port]
            tag = audit.release_tag(url, template, new)[0]
            port_when, port_how = tag_date(url, tag)
            if port_when > when:
                when, how = port_when, f"{port} {new}, {port_how}"
        return when, how

    commits = {p: audit.release_tag(url, t, pinned[p])[1] for p, (url, t) in shipped.items() if p in pinned}
    found = audit.osv_lookup(c for c in commits.values() if c)
    vulnerable_now = {f"{p}:{vid}" for p, c in commits.items() for vid, aliases, _, fixed in found.get(c, [])
                      if fixed and not ({vid, *(a.strip() for a in aliases.split(",") if a.strip())} & answered(p))}

    def fixes(version):
        still = set()
        for item in vulnerable_now:
            port, vid = item.split(":", 1)
            if port in moved(version):
                url, template = shipped[port]
                still |= {f"{port}:{v}" for v in fixable(audit.release_tag(url, template, moved(version)[port][1])[1])}
            else:
                still.add(item)
        return not (still & vulnerable_now)
    target = choose("vcpkg", None, candidates, released, vulnerable_now, fixes, report)
    if target is None:
        report.row("vcpkg baseline (every other port)", baseline[:10], latest, "held (cooldown)")
        return
    tag, commit = tags[target]
    report.row("vcpkg baseline (every other port)", baseline[:10], latest, f"**→ {tag}** (`{commit[:10]}`)")
    report.notes.append(f"vcpkg {tag}: the libraries without an overlay (libjxl, libwebp, lcms and every codec) move "
                        "to its versions; an overlay port whose version vcpkg now matches can be dropped (D-46), "
                        "unless it carries patches of ours (imageviewer-*.patch, docs/PLAN.md section 15).")
    report.review.append(f"- **vcpkg** `{baseline[:10]}` → `{tag}`: the port recipes run in our build; "
                         f"{VCPKG}/compare/{baseline}...{commit}")
    for port, (old, new) in sorted(moved(target).items()):
        url, template = shipped[port]
        if old is None:
            report.review.append(f"- **{port}** {new}: new in the packages; read it whole.")
            continue
        try:
            review_changes(port, url, audit.release_tag(url, template, old)[0],
                           audit.release_tag(url, template, new)[0], report)
        except subprocess.CalledProcessError:
            report.review.append(f"- **{port}** {old} → {new}: could not list its changes ({url}); review by hand.")
    report.changed = True
    if apply:
        write(path, read(path).replace(baseline, commit))
        if "vcpkg" in report.exceptions:
            record_exception([f"vcpkg port recipes {commit[:10]}"] + [f"{p} {new}" for p, (_, new) in moved(target).items()],
                             "security: fixes " + ", ".join(report.exceptions["vcpkg"][1]))


def update_qt(apply, report):
    pin = os.path.join(ROOT, "packaging", "qt-version.txt")
    pinned = read(pin).strip()
    current = version_tuple(pinned)
    tags = audit.release_tags("https://github.com/qt/qtbase", "v{v}")
    overall = max(tags) if tags else current
    held = f" ({as_text(overall)}: a new minor version, for a person — the private QRhi API may change)" \
        if overall[:2] > current[:2] else ""
    candidates = sorted(v for v in tags if v > current and v[:2] == current[:2])
    vulnerable_now = fixable(tags.get(current, (None, None))[1])
    target = choose("Qt", current, candidates, lambda v: tag_date("https://github.com/qt/qtbase", tags[v][0]),
                    vulnerable_now, lambda v: not (fixable(tags[v][1]) & vulnerable_now), report)
    if target is None:
        report.row("Qt", pinned, as_text(overall), ("held (cooldown)" if candidates else "up to date") + held)
        return
    new = as_text(target)
    report.row("Qt", pinned, as_text(overall), f"**→ {new}**" + held)
    report.notes.append(f"Qt {new}: the build installs it with aqtinstall, whose mirrors can lag a release by a few days.")
    report.review.append(f"- **Qt** {pinned} → {new}: read the release notes (https://code.qt.io/cgit/qt/qtreleasenotes.git) "
                         f"for security fixes and changes to the private QRhi API.")
    report.changed = True
    if apply:
        write(pin, new + "\n")
        if "Qt" in report.exceptions:
            record_exception([f"Qt {new}"], "security: fixes " + ", ".join(report.exceptions["Qt"][1]))
        licence = os.path.join(ROOT, "packaging", "licenses", "Qt.txt")
        write(licence, read(licence).replace(pinned, new))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--apply", action="store_true", help="edit the pins (otherwise only report)")
    parser.add_argument("--report", help="write the Markdown report to this file")
    args = parser.parse_args()
    report = Report()
    if MIN_RELEASE_AGE_DAYS < audit.POLICY_DAYS:
        report.notes.append(f"**Cooldown lowered to {MIN_RELEASE_AGE_DAYS} days** (packaging/cooldown-days.txt; the "
                            f"policy is {audit.POLICY_DAYS}, D-67): releases younger than {audit.POLICY_DAYS} days "
                            f"are taken. Set it back to {audit.POLICY_DAYS} once the release that needed it is out.")
    checks = [(p, lambda p=p, u=u, t=t: update_git_port(p, u, t, args.apply, report))
              for p, (u, t) in audit.COMMIT_PORTS.items()]
    checks += [("Rust", lambda: update_rust(args.apply, report)),
               ("graphicsmagick", lambda: update_tarball_port("graphicsmagick", audit.gm_releases, GM_ARCHIVE, args.apply, report)),
               ("vcpkg baseline", lambda: update_vcpkg_baseline(args.apply, report)),
               ("Qt", lambda: update_qt(args.apply, report)),
               ("cooldown exceptions", lambda: prune_exceptions(args.apply, report))]
    failed = False
    for name, check in checks:
        try:
            check()
        except Exception as error:  # one unreachable upstream must not hide the others
            failed = True
            report.row(name, "?", "?", "check failed")
            report.notes.append(f"{name}: the check failed ({error}); left as it is.")
    text = report.markdown()
    print(text)
    if args.report:
        write(args.report, text)
    if "GITHUB_OUTPUT" in os.environ:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"changed={'true' if report.changed else 'false'}\nfailed={'true' if failed else 'false'}\n"
                      f"exception={'true' if report.exception else 'false'}\ncooldown_days={MIN_RELEASE_AGE_DAYS}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
