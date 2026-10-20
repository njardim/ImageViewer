#!/usr/bin/env python3
"""Checks every pinned dependency against its upstream releases and, with --apply, moves it to
the latest stable one (D-46, D-62). Run by .github/workflows/dependency-update.yml when an issue
gets the "auto update" label; it also runs locally (without --apply it only reports).

What is pinned, and where:
- overlay ports in packaging/vcpkg/ports (OpenImageIO, libheif, expat, minizip-ng: a git tag's
  commit; GraphicsMagick and libaom: a release archive and its SHA-512);
- every other vcpkg port through the manifest's builtin-baseline (a vcpkg release tag's commit);
- Qt in packaging/qt-version.txt (read by .github/workflows/build.yml, whose files the workflow's
  token may not change) and packaging/licenses/Qt.txt.

Policy: libraries move to their latest stable release within the same major version; Qt only to
the latest patch release of its minor version (the viewer uses Qt's private QRhi API, D-09).
Anything newer than that is reported and left for a person. An overlay port's own patches are
tried on the new source in order: a patch upstream already contains is removed (D-57), one that
no longer applies is reported (the build will fail on it).

usage: python3 scripts/update_dependencies.py [--apply] [--report FILE]
Writes changed=true|false to $GITHUB_OUTPUT when it is set.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTS = os.path.join(ROOT, "packaging", "vcpkg", "ports")

# Overlay ports fetched by git: name, repository, stable tag pattern (its groups are the version).
GIT_PORTS = [
    ("openimageio", "https://github.com/AcademySoftwareFoundation/OpenImageIO", r"v(\d+)\.(\d+)\.(\d+)\.(\d+)"),
    ("libheif", "https://github.com/strukturag/libheif", r"v(\d+)\.(\d+)\.(\d+)"),
    ("expat", "https://github.com/libexpat/libexpat", r"R_(\d+)_(\d+)_(\d+)"),
    ("minizip-ng", "https://github.com/zlib-ng/minizip-ng", r"(\d+)\.(\d+)\.(\d+)"),
]
GM_RSS = "https://sourceforge.net/projects/graphicsmagick/rss?path=/graphicsmagick&limit=100"  # more: HTTP 400
GM_ARCHIVE = "https://downloads.sourceforge.net/project/graphicsmagick/graphicsmagick/{0}/GraphicsMagick-{0}.tar.xz"
AOM_LISTING = "https://storage.googleapis.com/aom-releases/"  # the bucket's XML listing (googlesource has no tag API)
AOM_ARCHIVE = "https://storage.googleapis.com/aom-releases/libaom-{0}.tar.gz"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "ImageViewer-dependency-update"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def git(*args, cwd=None):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def stable_tags(url, pattern):
    """{version tuple: (tag, commit)} for the tags matching the pattern; annotated tags peeled."""
    tags = {}
    for line in git("ls-remote", "--tags", url).splitlines():
        commit, ref = line.split("\t")
        name = ref.removeprefix("refs/tags/")
        name = name.removesuffix("^{}")
        match = re.fullmatch(pattern, name)
        if match:  # an annotated tag's peeled line ("^{}", the commit) comes after the tag object's
            tags[tuple(int(g) for g in match.groups())] = (name, commit)
    return tags


def version_tuple(text):
    return tuple(int(part) for part in text.split("."))


def as_text(version):
    return ".".join(str(part) for part in version)


def newest(versions, current, same_prefix):
    """The newest version sharing the current one's first `same_prefix` parts, and the newest overall."""
    allowed = [v for v in versions if v[:same_prefix] == current[:same_prefix]]
    return (max(allowed) if allowed else current), (max(versions) if versions else current)


class Report:
    def __init__(self):
        self.rows, self.notes, self.changed = [], [], False

    def row(self, name, pinned, latest, action):
        self.rows.append(f"| {name} | {pinned} | {latest} | {action} |")

    def markdown(self):
        lines = ["| Dependency | Pinned | Latest stable | Action |", "|---|---|---|---|", *self.rows]
        if self.notes:
            lines += ["", *[f"- {note}" for note in self.notes]]
        return "\n".join(lines) + "\n"


def check_patches(port, url, tag, apply, report):
    """Tries the port's patches, in the portfile's order, on the new source."""
    portfile = os.path.join(PORTS, port, "portfile.cmake")
    text = read(portfile)
    patches = [p for p in re.findall(r"([\w.-]+\.(?:patch|diff))\b", text) if os.path.exists(os.path.join(PORTS, port, p))]
    if not patches:
        return
    with tempfile.TemporaryDirectory() as source:
        git("clone", "--quiet", "--depth", "1", "--branch", tag, url, source)
        for patch in dict.fromkeys(patches):
            path = os.path.join(PORTS, port, patch)
            options = ["--ignore-whitespace", "--whitespace=nowarn"]
            if subprocess.run(["git", "apply", "--check", *options, path], cwd=source, capture_output=True).returncode == 0:
                subprocess.run(["git", "apply", *options, path], cwd=source, check=True, capture_output=True)
                # Ours, with an upstream fix named in the portfile ("upstream #5555"): that fix may
                # differ from ours and still leave our patch applicable.
                if upstream := re.search(rf"{re.escape(patch)}.*upstream (#\d+)", text):
                    report.notes.append(f"{port}: `{patch}` still applies to {tag}; check whether upstream "
                                        f"{upstream.group(1)} is in {tag} and remove the patch if it is (D-57).")
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


def update_git_port(port, url, pattern, apply, report):
    manifest = os.path.join(PORTS, port, "vcpkg.json")
    pinned = json.loads(read(manifest))["version"]
    current = version_tuple(pinned)
    tags = stable_tags(url, pattern)
    target, overall = newest(list(tags), current, 1)
    held = f" ({as_text(overall)} is a new major version: for a person)" if overall > target else ""
    if target <= current:
        report.row(port, pinned, as_text(overall), "up to date" + held)
        return
    new = as_text(target)
    tag, commit = tags[target]
    _, old_tag = re.search(r"REF ([0-9a-f]{40}) # (\S+)", read(os.path.join(PORTS, port, "portfile.cmake"))).groups()
    report.row(port, pinned, as_text(overall), f"**→ {new}**" + held)
    report.changed = True
    if apply:
        portfile = os.path.join(PORTS, port, "portfile.cmake")
        text = re.sub(r"REF [0-9a-f]{40} # \S+", f"REF {commit} # {tag}", read(portfile))
        # FETCH_REF names the tag where it does not use ${VERSION} (expat); "Overlay" comments name the version.
        text = "\n".join(line.replace(old_tag, tag) if "FETCH_REF" in line
                         else line.replace(pinned, new) if "Overlay" in line else line for line in text.split("\n"))
        write(portfile, text)
        write(manifest, read(manifest).replace(f'"version": "{pinned}"', f'"version": "{new}"'))
        update_manifest_comment(port, pinned, new)
    check_patches(port, url, tag, apply, report)


def update_manifest_comment(port, old, new):
    """The root manifest's comment on an overlay port names its version."""
    path = os.path.join(ROOT, "vcpkg.json")
    lines = read(path).split("\n")
    write(path, "\n".join(line.replace(old, new) if f"packaging/vcpkg/ports/{port}" in line else line for line in lines))


def update_tarball_port(port, listing, pattern, archive, apply, report):
    """An overlay port built from a release tarball: the newest release in `listing` (versions
    matched by `pattern`) within the pinned major version; the new archive's SHA-512 is computed."""
    manifest = os.path.join(PORTS, port, "vcpkg.json")
    data = json.loads(read(manifest))
    key = "version-semver" if "version-semver" in data else "version"
    pinned = data[key]
    versions = {version_tuple(v) for v in re.findall(pattern, fetch(listing).decode())}
    target, overall = newest(list(versions), version_tuple(pinned), 1)
    if target <= version_tuple(pinned):
        report.row(port, pinned, as_text(overall), "up to date")
        return
    new = as_text(target)
    report.row(port, pinned, as_text(overall), f"**→ {new}**")
    report.changed = True
    if apply:
        sha512 = hashlib.sha512(fetch(archive.format(new))).hexdigest()
        portfile = os.path.join(PORTS, port, "portfile.cmake")
        write(portfile, re.sub(r"SHA512 [0-9a-f]{128}", f"SHA512 {sha512}", read(portfile)))
        write(manifest, read(manifest).replace(f'"{key}": "{pinned}"', f'"{key}": "{new}"'))
        update_manifest_comment(port, pinned, new)


def update_vcpkg_baseline(apply, report):
    path = os.path.join(ROOT, "vcpkg.json")
    baseline = json.loads(read(path))["builtin-baseline"]
    tags = stable_tags("https://github.com/microsoft/vcpkg", r"(\d{4})\.(\d{2})\.(\d{2})")
    tag, commit = tags[max(tags)]
    if commit == baseline:
        report.row("vcpkg baseline (every other port)", tag, tag, "up to date")
        return
    # The baseline may be a commit between two releases: move only when the release is newer, i.e.
    # when the baseline is its ancestor. Asked of git (commits only, no trees or files) rather than
    # GitHub's API, which needs a token outside Actions.
    with tempfile.TemporaryDirectory() as work:
        git("init", "-q", work)
        git("fetch", "-q", "--filter=tree:0", "https://github.com/microsoft/vcpkg", baseline, commit, cwd=work)
        newer = subprocess.run(["git", "merge-base", "--is-ancestor", baseline, commit], cwd=work).returncode == 0
    if not newer:
        report.row("vcpkg baseline (every other port)", baseline[:10], tag, f"up to date ({tag} is not newer)")
        return
    report.row("vcpkg baseline (every other port)", baseline[:10], tag, f"**→ {tag}** (`{commit[:10]}`)")
    report.notes.append(f"vcpkg {tag}: the libraries without an overlay (libjxl, libwebp, lcms and every codec) move "
                        "to its versions; an overlay port whose version vcpkg now matches can be dropped (D-46).")
    report.changed = True
    if apply:
        write(path, read(path).replace(baseline, commit))


def update_qt(apply, report):
    pin = os.path.join(ROOT, "packaging", "qt-version.txt")
    pinned = read(pin).strip()
    versions = list(stable_tags("https://github.com/qt/qtbase", r"v(\d+)\.(\d+)\.(\d+)"))
    target, overall = newest(versions, version_tuple(pinned), 2)
    held = f" ({as_text(overall)}: a new minor version, for a person — the private QRhi API may change)" \
        if overall > target else ""
    if target <= version_tuple(pinned):
        report.row("Qt", pinned, as_text(overall), "up to date" + held)
        return
    new = as_text(target)
    report.row("Qt", pinned, as_text(overall), f"**→ {new}**" + held)
    report.notes.append(f"Qt {new}: the build installs it with aqtinstall, whose mirrors can lag a release by a few days.")
    report.changed = True
    if apply:
        write(pin, new + "\n")
        licence = os.path.join(ROOT, "packaging", "licenses", "Qt.txt")
        write(licence, read(licence).replace(pinned, new))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--apply", action="store_true", help="edit the pins (otherwise only report)")
    parser.add_argument("--report", help="write the Markdown report to this file")
    args = parser.parse_args()
    report = Report()
    checks = [(p, lambda p=p, u=u, t=t: update_git_port(p, u, t, args.apply, report)) for p, u, t in GIT_PORTS]
    checks += [("graphicsmagick", lambda: update_tarball_port("graphicsmagick", GM_RSS, r"GraphicsMagick-(\d+\.\d+\.\d+)\.tar\.xz",
                                                          GM_ARCHIVE, args.apply, report)),
               ("aom", lambda: update_tarball_port("aom", AOM_LISTING, r"libaom-(\d+\.\d+\.\d+)\.tar\.gz",
                                               AOM_ARCHIVE, args.apply, report)),
               ("vcpkg baseline", lambda: update_vcpkg_baseline(args.apply, report)),
               ("Qt", lambda: update_qt(args.apply, report))]
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
            out.write(f"changed={'true' if report.changed else 'false'}\nfailed={'true' if failed else 'false'}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
