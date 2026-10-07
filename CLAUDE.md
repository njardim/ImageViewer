# CLAUDE.md — imageViewer

**Read `docs/PLAN.md` first.** It is the single source of truth: current status (§1), decision log (§2), architecture (§6), fidelity criteria (§7), phases with checkboxes (§9), and pending decisions (§13). Do not re-derive decisions recorded there; do not re-open them without new evidence.

## What this is
Cross-platform (Windows, macOS, Linux) image viewer in **C++20 + Qt 6.11**, minimalist UI, **verifiable colour fidelity in SDR and HDR**, format coverage ≥ qView ∪ ImageGlass ∪ FFmpeg image2. Owner: Nuno Jardim (Cristallumnis). **Open source under Apache-2.0** (decision D-18): `LICENSE` + `NOTICE` ship in every package; the Cristallumnis trademark is not licensed; future Cristallumnis AI modules are separate proprietary plugins (open core); contributions need a DCO sign-off.

## Rules
- **Language (D-27): everything in the repository is 100 % English** — code, identifiers, comments, UI source strings, docs, commit messages, PR texts. Nothing in Portuguese is committed (the Portuguese UI lives only in `translations/imageviewer_pt.ts`). Talk to the user in the language they write in (Nuno: European Portuguese).
- **UI text (D-27):** every user-visible string goes through `tr()` (QObject classes) or `QCoreApplication::translate("Context", …)`; numbers through `QLocale`; never build sentences from translated fragments (join complete segments outside `tr`); add a `//:` translator comment when a string carries syntax (`&&`, `;;`, units). After changing UI strings: `cmake --build <dir> --target update_translations`, translate the 15 languages, run `tests/check_translations.py`. `--info`/`--render` output stays English (tests parse it).
- **Keep the structure minimal** (decision D-08): one executable target, flat `src/`, one `CMakeLists.txt`. Split a file only when it exceeds ~800 lines. No new directories or abstraction layers without a recorded decision. `packaging/` (D-20) holds vcpkg overlays, third-party licence texts and the CI licence gate — never application code. `translations/` (D-27) holds the Qt Linguist `.ts` files. `ViewerWindow` is split in `viewer.cpp` (display, view state, input) and `commands.cpp` (command table, menus, file operations, dialogs) (D-30).
- **Never copy code from qView or ImageGlass** (GPL-3). They are behavioural references only (D-11): we offer their good features (D-28, parity matrix in `docs/PLAN.md` §8.1) designed and written our own way.
- **Commands (D-30):** a user action is one `ViewerWindow::Command`: add it to `commands()` (shortcuts), `commandText()`, `isCommandEnabled()`, `execute()` and to the right submenu in `showContextMenu()`. Keyboard and menus then stay in sync.
- **Settings (D-29):** a new preference is a `Settings` field with a default, validated in `Settings::load()`, saved in `save()`, editable in `SettingsDialog`. Tests that launch the GUI set `XDG_CONFIG_HOME` (and `HOME`/`XDG_DATA_HOME` when they delete) to a temporary directory.
- **Licences:** only LGPL (dynamically linked) or permissive dependencies (GPL is incompatible with shipping under Apache-2.0). Excluded: exiv2, FFmpeg `gpl`/`nonfree`, x265, LibRaw GPL packs, Ghostscript, PyQt.
- **Colour pipeline invariant:** decoders output native depth + colour descriptor + **straight alpha** (D-21); conversion to **linear scRGB (RGBA16F, premultiplied)** happens once on the CPU; the shader only does exposure, tone mapping, compositing over the background in linear light and output encoding (ScRGB / PQ / SdrIcc). Never quantize below FP16 before output. Descriptor priority: CICP > ICC > format attributes > assumed (D-22).
- **Releases** (D-19, D-26): versions are X.Y[-suffix], never X.Y.Z; the version comes from the tag. Publishing a GitHub release `vX.Y[-suffix]` (or pushing the tag) builds, verifies and attaches the packages; nobody builds release binaries by hand.
- **Licences in practice:** a dependency that pulls default features can silently override `"default-features": false` in our manifest (that is how x265 got in, D-25); the CI licence gate must stay green.
- **Viewer is a `QWindow` with its own QRhi swapchain** (D-09) — Widgets cannot do HDR. Dialogs/menus may use Widgets.
- **Evidence:** when adding claims to the plan, tag them `[code]`, `[doc]`, `[test]`, `[knowledge]` or `[inference]`.

## End-of-session duty
Update `docs/PLAN.md`: §1 (status, next steps, blockers), §2 (new decisions with date and reason), §9 checkboxes, §13/§14 as needed. Commit it with the code.

## Build
- Production deps via vcpkg manifest (`vcpkg.json`), dynamic triplets; Qt from official binaries (aqtinstall) in CI.
- Local Linux/cloud sessions: `download.qt.io` and GitHub release assets are blocked by the proxy. Build Qt from source with `scripts/build-qt-linux.sh` (installs to `/opt/qt6`), and use Ubuntu apt packages for OIIO/FFmpeg/lcms2 when compiling locally:
  ```
  cmake --preset linux-system && cmake --build --preset linux-system
  ```
- Tests: `tests/smoke.sh <exe>` (headless decode/colour checks), `tests/screen_test.py <exe> [vulkan|opengl]` (Xvfb, on-screen pixel exactness at 100 %), `tests/render_test.py <exe> [vulkan|opengl]` (Xvfb, offscreen render of every output mode vs. the CPU reference `color::applyOutputStage` and the BT.2390 spec), `tests/ui_test.py <exe> [vulkan|opengl]` (Xvfb + xdotool: side zones, flips/rotations, trash with confirmation, saved session) and `tests/check_translations.py` (placeholders and completeness of the `.ts` files). Shader and `applyOutputStage` must stay in lockstep. The local Qt needs LinguistTools: build qttools' `lupdate`/`lrelease` into `/opt/qt6` if missing.
- CI: `.github/workflows/build.yml` (Windows x64, macOS arm64, Linux x64).

## Git
Work on the branch named in the session instructions; commit with clear messages; push with `git push -u origin <branch>`.
