# Security policy

ImageViewer opens files from anywhere, so every file it reads is treated as hostile input, and the
libraries that read them are treated as part of the attack surface.

## Reporting a vulnerability

**Do not open a public issue.** Report it privately through GitHub:
[Security › Report a vulnerability](https://github.com/njardim/ImageViewer/security/advisories/new).
Include the version (`ImageViewer --version`), the system, and a file or steps that reproduce it.

- Acknowledgement within **3 working days**.
- An assessment (confirmed or not, and its severity) within **10 working days**.
- A fix released as soon as it is ready, normally within **30 days** of confirmation for a high or
  critical issue; the advisory is published with the release and credits you unless you prefer
  otherwise.

## In scope

- Memory safety or code execution when opening, decoding or displaying a file, a folder or a
  file name (including the decode worker process).
- A vulnerability in a library the packages ship (OpenImageIO, libjxl, libwebp, libheif, libaom,
  GraphicsMagick, Qt, and the others listed in each package's bill of materials), when ImageViewer
  is affected.
- File operations (delete, rename, move, trash), "Open with", settings files and command-line
  arguments.
- The build and release pipeline: anything that could put code we did not write into a package.

Out of scope: denial of service through a very large file the viewer reports as too large, and
issues that need an attacker who already controls the user's account.

## Supported versions

Only the latest release receives fixes until version 1.0.

## How the supply chain is protected

Decision D-67 in [`docs/PLAN.md`](docs/PLAN.md) has the details and the reasons.

- **Cooldown.** A new release of a library, of Qt, of vcpkg's recipes or of a GitHub Action is
  taken only once it is 15 days old, unless it fixes a known vulnerability that cannot wait; such
  an exception is recorded in `packaging/cooldown-exceptions.txt`, its changes are read by hand,
  and it is removed once its release is 15 days old (D-73).
  The libraries' cooldown is the number in `packaging/cooldown-days.txt`: the maintainer may lower
  it on `main` for a release that should take the latest releases sooner, and every such change is
  a commit; while it is below 15 days, the weekly audit opens an issue until it is set back (D-72).
- **Audit** (`scripts/audit_dependencies.py`, weekly and on every change to the pins): every shipped
  library is resolved to the exact commit it is built from and looked up in
  [OSV](https://osv.dev); libraries built from release archives are compared file by file with
  their repository's tag; nothing is installed that the audit does not follow.
- **Every known vulnerability is answered** (D-73): updated away, fixed by a patch of ours, or
  assessed by a person in `packaging/known-vulnerabilities.txt` (out of reach, with the reason, or
  an accepted risk). A new one nobody has assessed stops the release and opens an issue.
- **Our patches to libraries** are `imageviewer-*.patch` files in their overlay ports, each naming
  what it fixes, its upstream fix and when it goes; `docs/PLAN.md` §15 lists them, and each
  package's bill of materials names the ones built into it. Today: OpenJPH (CVE-2026-111151 and
  two upstream fixes), LibRaw (CVE-2026-111272), OpenJPEG (OSV-2025-219) and OpenImageIO (two).
- **Release gate.** A release is published only when the audit is clean.
- **Only audited code reads images.** Qt reads only SVG, ICNS, cursors, WBMP, XPM and XBM; the PNG,
  JPEG, GIF and WebP images inside them are decoded by OpenImageIO with the audited libraries, never
  by Qt's own copies, and the packages carry no other Qt image plugin (D-68, D-71).
- **An SVG reads nothing outside itself.** Before Qt renders an SVG, every reference to another
  file (an image beside it or at any path, a network share, a style sheet, an SVG inside it that
  names one) is taken out, as browsers show an SVG used as an image; an svgz may inflate to at most
  256 MiB (D-71).
- **Never below a security fix.** Each library is pinned to its newest release past the cooldown;
  when that would drop a security fix, the newer release ships as a recorded exception, approved
  by the maintainer after reading its changes (D-69).
- **Verifiable packages.** Every release attaches `SHA256SUMS`; each package's build provenance and
  its SPDX bill of materials are attested with it. The second command also prints the bill of
  materials (the release does not carry it as a separate file, D-74):

  ```
  gh attestation verify ImageViewer-0.8-linux-x64.tar.gz -R njardim/ImageViewer
  gh attestation verify ImageViewer-0.8-linux-x64.tar.gz -R njardim/ImageViewer \
    --predicate-type https://spdx.dev/Document/v2.3 --format json
  ```
- **Code scanning.** CodeQL analyses the C++ and the workflows; OpenSSF Scorecard grades the
  repository's practices; every action is pinned by commit; workflow tokens have the least
  permissions they need.
