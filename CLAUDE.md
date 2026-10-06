# CLAUDE.md — imageViewer

**Read `docs/PLANO.md` first.** It is the single source of truth: current status (§1), decision log (§2), architecture (§6), fidelity criteria (§7), phases with checkboxes (§9), and pending decisions (§13). Do not re-derive decisions recorded there; do not re-open them without new evidence.

## What this is
Cross-platform (Windows, macOS, Linux) image viewer in **C++20 + Qt 6.11**, minimalist UI, **verifiable colour fidelity in SDR and HDR**, format coverage ≥ qView ∪ ImageGlass ∪ FFmpeg image2. Owner: Nuno Jardim (Cristallumnis). Product is proprietary (see pending decision D-P01 about the repo's MIT LICENSE).

## Rules
- **Language:** talk to the user and write docs in European Portuguese (pt-PT); code, identifiers, comments and commit messages in English.
- **Keep the structure minimal** (decision D-08): one executable target, flat `src/`, one `CMakeLists.txt`. Split a file only when it exceeds ~800 lines. No new directories or abstraction layers without a recorded decision.
- **Never copy code from qView or ImageGlass** (GPL-3). They are behavioural references only (D-11).
- **Licences:** only LGPL (dynamically linked) or permissive dependencies. Excluded: exiv2, FFmpeg `gpl`/`nonfree`, x265, LibRaw GPL packs, Ghostscript, PyQt.
- **Colour pipeline invariant:** decoders output native depth + colour descriptor; conversion to **linear scRGB (RGBA16F, premultiplied)** happens once on the CPU; the shader only does exposure, tone mapping and output encoding (ScRGB / PQ / SdrIcc). Never quantize below FP16 before output.
- **Viewer is a `QWindow` with its own QRhi swapchain** (D-09) — Widgets cannot do HDR. Dialogs/menus may use Widgets.
- **Evidence:** when adding claims to the plan, tag them `[código]`, `[doc]`, `[teste]`, `[conhecimento]` or `[inferência]`.

## End-of-session duty
Update `docs/PLANO.md`: §1 (status, next steps, blockers), §2 (new decisions with date and reason), §9 checkboxes, §13/§14 as needed. Commit it with the code.

## Build
- Production deps via vcpkg manifest (`vcpkg.json`), dynamic triplets; Qt from official binaries (aqtinstall) in CI.
- Local Linux/cloud sessions: `download.qt.io` and GitHub release assets are blocked by the proxy. Build Qt from source with `scripts/build-qt-linux.sh` (installs to `/opt/qt6`), and use Ubuntu apt packages for OIIO/FFmpeg/lcms2 when compiling locally:
  ```
  cmake --preset linux-system && cmake --build --preset linux-system
  ```
- Tests: `tests/smoke.sh <exe>` (headless decode/colour checks) and `tests/screen_test.py <exe> [vulkan|opengl]` (Xvfb, on-screen pixel exactness at 100 %).
- CI: `.github/workflows/build.yml` (Windows x64, macOS arm64, Linux x64).

## Git
Work on the branch named in the session instructions; commit with clear messages; push with `git push -u origin <branch>`.
