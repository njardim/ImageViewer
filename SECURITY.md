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
  an exception is recorded in `scripts/audit_dependencies.py` and its changes are read by hand.
- **Audit** (`scripts/audit_dependencies.py`, weekly and on every change to the pins): every shipped
  library is resolved to the exact commit it is built from and looked up in
  [OSV](https://osv.dev); libraries built from release archives are compared file by file with
  their repository's tag; nothing is installed that the audit does not follow.
- **Release gate.** A release is published only when the audit is clean.
- **Only audited code reads images.** Qt reads only SVG, ICNS, cursors, WBMP, XPM and XBM; the PNG
  images inside them are decoded by OpenImageIO with the audited libpng, never by Qt's own copy, and
  the packages carry no other Qt image plugin (D-68).
- **Never below a security fix.** Each library is pinned to its newest release past the cooldown;
  when that would drop a security fix, the newer release ships as a recorded exception, approved
  by the maintainer after reading its changes (D-69).
- **Verifiable packages.** Every release attaches `SHA256SUMS` and, for each package, an SPDX bill of
  materials (`*.spdx.json`); both the build provenance and the bill of materials are attested:

  ```
  gh attestation verify ImageViewer-0.8-linux-x64.tar.gz -R njardim/ImageViewer
  gh attestation verify ImageViewer-0.8-linux-x64.tar.gz -R njardim/ImageViewer \
    --predicate-type https://spdx.dev/Document/v2.3
  ```
- **Code scanning.** CodeQL analyses the C++ and the workflows; OpenSSF Scorecard grades the
  repository's practices; every action is pinned by commit; workflow tokens have the least
  permissions they need.
