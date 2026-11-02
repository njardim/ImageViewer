#!/usr/bin/env python3
"""What scripts/audit_dependencies.py and scripts/update_dependencies.py share (D-67, D-102): the
packaging/ data files they read (the cooldown, its exceptions, the assessed vulnerabilities), how
they reach upstream (HTTPS without a token leaving GitHub's API, git tags and their dates,
GraphicsMagick's archives, crates.io), OSV's queries, the SVG renderer's lock and toolchain, and the
report they write. Split from audit_dependencies.py (D-08); a module, not a script.
"""
import datetime
import email.utils
import json
import os
import re
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTS = os.path.join(ROOT, "packaging", "vcpkg", "ports")
OSV = "https://api.osv.dev/v1"
COOLDOWN_FILE = os.path.join(ROOT, "packaging", "cooldown-days.txt")
EXCEPTIONS_FILE = os.path.join(ROOT, "packaging", "cooldown-exceptions.txt")
KNOWN_FILE = os.path.join(ROOT, "packaging", "known-vulnerabilities.txt")
VULNERABILITY_ID = re.compile(r"\b(?:CVE-\d{4}-\d+|OSV-\d{4}-\d+|RUSTSEC-\d{4}-\d{4}|GHSA(?:-[0-9a-z]{4}){3})\b")
GM_RSS = "https://sourceforge.net/projects/graphicsmagick/rss?path=/graphicsmagick&limit=100"  # more: HTTP 400
# The SVG renderer's crates and toolchain (D-90).
SVG_PORT = os.path.join(PORTS, "imageviewer-svg")
CRATES_IO = "https://crates.io/api/v1/crates"
RUST = "https://github.com/rust-lang/rust"
USER_AGENT = "ImageViewer-dependency-audit (https://github.com/njardim/ImageViewer)"  # crates.io asks for a contact


def data_lines(path):
    """The lines of a data file of packaging/, without its comment lines ("#") and blank lines."""
    with open(path, encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip() and not line.lstrip().startswith("#")]


def cooldown_days():
    """The cooldown (D-67): the one whole number, 0 to 365, of packaging/cooldown-days.txt, after
    its comment lines. Anything else stops the audit, which never passes by default (exit 2)."""
    values = data_lines(COOLDOWN_FILE)
    if len(values) != 1 or not re.fullmatch(r"\d{1,3}", values[0]) or int(values[0]) > 365:
        raise ValueError(f"{COOLDOWN_FILE}: expected one whole number of days, 0 to 365, found {values}")
    return int(values[0])


def cooldown_exceptions():
    """{"library version": reason} of packaging/cooldown-exceptions.txt (D-73): releases allowed to
    ship younger than the cooldown, each a security fix (or the release one needs, D-69) that a
    person read and the owner approved. scripts/update_dependencies.py adds and removes them."""
    entries = {}
    for line in data_lines(EXCEPTIONS_FILE):
        key, separator, reason = line.partition(": ")
        if not separator or not re.fullmatch(r"\S+( \S+)* \S+", key) or not reason.strip() or key in entries:
            raise ValueError(f"{EXCEPTIONS_FILE}: expected '<library> <version>: <reason>' once each, found {line!r}")
        entries[key] = reason.strip()
    return entries


def known_vulnerabilities():
    """{(library, id): assessment} of packaging/known-vulnerabilities.txt (D-73): known
    vulnerabilities a person assessed as out of the packages' reach or as an accepted risk."""
    entries = {}
    for line in data_lines(KNOWN_FILE):
        match = re.fullmatch(r"(\S+) (\S+): (.+)", line)
        if not match or not VULNERABILITY_ID.fullmatch(match.group(1)) or (match.group(2), match.group(1)) in entries:
            raise ValueError(f"{KNOWN_FILE}: expected '<id> <library>: <assessment>' once each, found {line!r}")
        entries[(match.group(2), match.group(1))] = match.group(3)
    return entries


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class SameHostRedirects(urllib.request.HTTPRedirectHandler):
    """Follows a redirect without the Authorization header when it leaves the host: urllib copies
    every header to the new address, so a token would otherwise go wherever a redirect points."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and urllib.parse.urlsplit(newurl).netloc != urllib.parse.urlsplit(req.full_url).netloc:
            new.remove_header("Authorization")
        return new


OPENER = urllib.request.build_opener(SameHostRedirects)


def fetch(url, data=None):
    headers = {"User-Agent": USER_AGENT}
    if data is not None:
        headers["Content-Type"] = "application/json"
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token and url.startswith("https://api.github.com/"):  # never sent to any other host
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, data=data, headers=headers)
    with OPENER.open(request, timeout=120) as response:
        return response.read()


def run(*command, cwd=None):
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True).stdout


def tag_commit(url, tag):
    """The commit a tag names (annotated tags peeled); None when the tag is missing or the host is
    unreachable (the caller reports it: an unresolved library is never a pass)."""
    try:
        lines = run("git", "ls-remote", "--tags", url, f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}").splitlines()
    except subprocess.CalledProcessError:
        return None
    return lines[-1].split("\t")[0] if lines else None


def release_tag(url, templates, version):
    """(tag, commit) of a version: the first of the "|"-separated tag templates that exists."""
    for template in templates.split("|"):
        tag = template.format(v=version, v_=version.replace(".", "_"))
        commit = tag_commit(url, tag)
        if commit:
            return tag, commit
    return templates.split("|")[0].format(v=version, v_=version.replace(".", "_")), None


def github_repository(url):
    match = re.fullmatch(r"https://github\.com/([^/]+/[^/]+?)(\.git)?", url)
    return match.group(1) if match else None


def tag_date(url, tag):
    """(when the tag was published, how that is known). GitHub's release date where the project
    made a release of the tag (set by GitHub: the publisher cannot backdate it); otherwise the tag's
    own date (the tagger's, or the commit's for a lightweight tag), which the publisher sets."""
    repository = github_repository(url)
    if repository:
        try:
            release = json.loads(fetch(f"https://api.github.com/repos/{repository}/releases/tags/{tag}"))
            if release.get("published_at"):
                return datetime.datetime.fromisoformat(release["published_at"].replace("Z", "+00:00")), "GitHub release"
        except urllib.error.HTTPError as error:
            if error.code not in (403, 404):  # 404: a tag without a release; 403: no token, rate limited
                raise
    return ref_date(url, f"refs/tags/{tag}"), "tag date"


def ref_date(url, ref):
    """The date of a tag (the tagger's, or its commit's) or of a commit, fetched without files."""
    with tempfile.TemporaryDirectory() as work:
        run("git", "init", "-q", work)
        run("git", "fetch", "-q", "--depth", "1", "--filter=tree:0", url, f"+{ref}:refs/audit", cwd=work)
        stamp = run("git", "for-each-ref", "refs/audit", "--format=%(creatordate:unix)", cwd=work).strip()
    return datetime.datetime.fromtimestamp(int(stamp), datetime.timezone.utc)


def gm_releases():
    """{version: upload date} of GraphicsMagick's archives on SourceForge (dates set by SourceForge)."""
    releases = {}
    for item in re.findall(r"<item>(.*?)</item>", fetch(GM_RSS).decode(), re.S):
        name = re.search(r"GraphicsMagick-(\d+\.\d+\.\d+)\.tar\.xz", item)
        date = re.search(r"<pubDate>([^<]+)</pubDate>", item)
        if name and date:
            releases[name.group(1)] = email.utils.parsedate_to_datetime(date.group(1))
    return releases


def age_days(when):
    return (datetime.datetime.now(datetime.timezone.utc) - when).total_seconds() / 86400


def crates():
    """[(name, version, checksum)] of every crate from crates.io in the SVG renderer's Cargo.lock (D-90),
    which builds resvg with `cargo build --locked`."""
    import tomllib  # Python 3.11
    with open(os.path.join(SVG_PORT, "Cargo.lock"), "rb") as f:
        lock = tomllib.load(f)
    return [(p["name"], p["version"], p.get("checksum")) for p in lock["package"]
            if p.get("source", "").startswith("registry+")]


def rust_version():
    """The Rust release that builds the SVG renderer (its rust-toolchain.toml, D-90)."""
    match = re.search(r'^channel = "(\d+\.\d+\.\d+)"$', read(os.path.join(SVG_PORT, "rust-toolchain.toml")), re.M)
    if not match:
        raise ValueError("packaging/vcpkg/ports/imageviewer-svg/rust-toolchain.toml: no Rust release pinned")
    return match.group(1)


def crate_date(name, version):
    """When crates.io published a crate's version (set by crates.io, not by the publisher). crates.io
    allows one request a second."""
    import time
    time.sleep(1)
    data = json.loads(fetch(f"{CRATES_IO}/{urllib.parse.quote(name)}/{urllib.parse.quote(version)}"))
    return datetime.datetime.fromisoformat(data["version"]["created_at"].replace("Z", "+00:00"))


def osv_lookup(commits):
    """{commit: [(id, CVE aliases, summary, fixed upstream)]} from OSV; withdrawn entries left out."""
    commits = list(dict.fromkeys(commits))
    return dict(zip(commits, osv_query([{"commit": c} for c in commits])))


def osv_crates(pinned):
    """{(name, version): [(id, aliases, summary, fixed upstream)]} of crates from crates.io, from OSV."""
    return dict(zip(pinned, osv_query([{"package": {"ecosystem": "crates.io", "name": n}, "version": v}
                                       for n, v in pinned])))


def osv_query(queries):
    """[[(id, CVE aliases, summary, fixed upstream)] per query] from OSV's batch query; withdrawn
    entries left out."""
    if not queries:
        return []
    results = json.loads(fetch(f"{OSV}/querybatch", json.dumps({"queries": queries}).encode()))["results"]
    details, found = {}, []
    for query, result in zip(queries, results):
        if result.get("next_page_token"):
            raise RuntimeError(f"OSV paginated the results of {query}: query it alone")
        found.append([])
        for vid in (v["id"] for v in result.get("vulns", [])):
            if vid not in details:
                details[vid] = json.loads(fetch(f"{OSV}/vulns/{vid}"))
            entry = details[vid]
            if entry.get("withdrawn"):
                continue
            fixed = any("fixed" in e for a in entry.get("affected", []) for r in a.get("ranges", [])
                        for e in r.get("events", []))
            aliases = ", ".join(a for a in entry.get("aliases", []) if a.startswith(("CVE-", "RUSTSEC-", "GHSA-")))
            summary = (entry.get("summary") or entry.get("details", "").split("\n")[0])[:120]
            found[-1].append((vid, aliases, summary, fixed))
    return found


class Report:
    def __init__(self):
        self.rows, self.notes = [], []
        self.stale = []  # cooldown exceptions that no longer apply (scripts/update_dependencies.py removes them)
