# imageViewer: action plan and project status

> **Living document and single source of truth.** Every new conversation or session starts here (`CLAUDE.md` points to `docs/PLAN.md`).
>
> At the end of every work session, update:
> - §1 (Current status);
> - §2 (Decision log), if there are new decisions;
> - the phase checkboxes (§9).
>
> Language: English for everything in the repository (code, comments, UI text, documentation). The UI is translated through Qt Linguist (decision D-27); conversations with the owner may be in Portuguese, but nothing in Portuguese is committed.

**Evidence tags:**

| Tag | Meaning |
|---|---|
| `[code]` | Source-code analysis (repository and commit given). |
| `[doc]` | Official documentation or README. |
| `[test]` | Measured in a Linux x64 container (4 vCPU, 2026-10-06). |
| `[knowledge]` | Prior knowledge, not verified in this project. |
| `[inference]` | Deduction from indirect evidence. |

---

## 1. Current status

| Field | Value |
|---|---|
| Date | 2026-10-08 |
| Phase | **0: Foundation** (startup time still to be measured), **1: Color and HDR core** and **2: Format coverage** (in progress) |
| Working branch | `claude/upbeat-bohr-tvnhkd` |
| Latest milestone | **Release `v0.3` published** (2026-10-08, merge of PR #4, D-40, formats first): a registry of 50 formats with detection by content (D-38), JPEG XL, animation of GIF, WebP, APNG, JPEG XL and AVIF, the slideshow, HEIC through the operating system (D-39), GraphicsMagick 1.3.48 in a sandboxed decode worker, a fuzz smoke test; the macOS quit crash fixed; side zones 100 px and Apply (D-36). Full adversarial review of the whole codebase (D-35) with five parallel reviewers, every finding reproduced before its fix (§9, "Release 0.3"; D-42 to D-45). **Before:** **Release `v0.2` published** (2026-10-07, merge of PR #3): preloading, information panel and E14 overlay, file actions, 37 languages. **Earlier:** **Release `v0.1` published** (2026-10-07, run 28, from `main` `51485b2`, merge of PR #2): packages Windows 42.5 MB, macOS 37.5 MB, Linux 49.0 MB, plus `SHA256SUMS` `[test]`. Contents (§9, "Release 0.1"): English-only repository and interface translated into 16 languages (D-27), qView feature set with grouped context menu and command table (D-28, D-30), Settings window and saved session (D-29), clickable side zones (D-31), new interaction test on Xvfb. **Earlier:** first published version `v0.1-alpha` (pre-release, 2026-10-06), produced only by CI from `main` (`3c0fd61`, merge of PR #1), with the 3 packages, `SHA256SUMS` and the provenance attestation `[test: run 19]`. Before that: C++/Qt 6.11 skeleton compiling without warnings. Color pipeline smoke tests pass (unbounded P3→scRGB, EXR > 1.0, EXIF orientation). **Screen test under Xvfb: 8-bit sRGB ramp at 100 % identical to the file, bit for bit, on Vulkan and OpenGL** (criterion F8 in SDR, Linux). CI for the 3 systems set up. **Phase 1: output stage with BT.2390 EETF and absolute PQ (D-15), verified by an offscreen harness on every output (D-16): 7/7 cases on Vulkan and OpenGL.** **Full adversarial review** (D-21 to D-26): alpha and compositing in linear light, CICP before ICC, matrix/curve ICC without LittleCMS, EDR layer on macOS, libheif without HEVC, GPU device loss, non-blocking decoding, third-party licenses in the packages, and automatic releases with package testing on clean machines. Locally: smoke tests 18/18, harness 7/7 and exact screen test on Vulkan and OpenGL `[test]`. |
| CI | **Runs 6 and 7 green on all 3 systems** (`de0fe5a`): build, smoke tests, screen test, `--render` harness and packages with `LICENSE` and `NOTICE` `[test]`. **Run 10** (`9903f3b`): compiles with MSVC, Apple clang and GCC; failed only on the old `hdr4.exr` expectations (premultiplied alpha is now read as such, D-21). **Run 11** (`3aec5a9`): rebuilds the vcpkg dependencies (libheif overlay, macOS 13.0 triplet). **Run 12** (`55099a0`, new workflow D-26): Linux and macOS green, including the harness on **Metal** and the license gate `[test]`. On Windows, the harness on **D3D11/WARP** passes all 7 cases (GPU = CPU, error ≤ 5.6e-5), but the independent neutrality check in the PQ case failed. The tolerance in nits (0.1 nit) was tighter than WARP's precision: 9.2e-6 in the PQ code value gives up to 0.3 nit at 1000 nits, one thirtieth of a 10-bit step `[test]`. The check is now done on the PQ signal (tolerance of 5e-5). **Run 13** (`7967c4b`): all 3 builds went green, including the D3D11/WARP harness. The **Windows and macOS packages start and pass the smoke tests on a clean machine**, which validates `INSTALL_RPATH` for `macdeployqt` and the MSVC runtime DLLs `[test]`. The Linux package depends on `libOpenGL.so.0` and `libEGL.so.1` (libglvnd, linked by Qt), which the runner's minimal image does not have. They are system libraries and are never bundled in the package: they are documented as a requirement and the `verify` job installs them. **Run 14** (`abe9e7c`): **all green**, with 3 builds and 3 packages verified on clean machines `[test]`. **Run 17** (`main`, after the merge): green, and the vcpkg cache from `main` becomes available to all branches. **Release:** publishing through the web interface fired both the `release` event (run 18) and the tag `push` event (run 19). The concurrency group canceled run 18, and run 19 did build, verify and release `[test]`. **Runs 21–23** (`e06944b`…`a531419`, release 0.1): all green on the 3 systems, including the interaction test (Linux, Vulkan and OpenGL), the translations gate and the 3 packages on clean machines; the Windows build now installs `qttools` for `lrelease` `[test]`. **Runs 49–57** (0.3): the quit test reproduced the macOS crash under lldb and passed after the fix (49–51); 51–53 and 55 green on the 3 systems, with HEIC decoded by ImageIO on the macOS runner; 54 failed on a raw apostrophe in the Turkish translation (the gate), 56 on the `min`/`max` macros of `windows.h` in `worker.cpp` (fixed with `NOMINMAX`) `[test]`. **Run 58** (the review fixes): the fuzz smoke test crashed OpenImageIO's Softimage reader on Linux and macOS (D-45), and on Windows, where the decode worker ran for the first time, none of the 16 GraphicsMagick formats decoded. **Run 59:** Linux and macOS green (fuzz clean with our PIC reader, quit test, packages). **Run 60:** `smoke.sh`'s new diagnostics (the worker run directly, with GraphicsMagick's event log) showed the cause: with MSVC, GraphicsMagick's `studio.h` turns module loading on for every `/MD` build, so no built-in coder was registered `[test]`; the overlay port now limits that to builds made with modules. **Run 62** (`a8a1e66`): **green on the 3 systems** — builds, smoke tests (the 16 GraphicsMagick formats on Windows too; HEIC through ImageIO on macOS), fuzz smoke test (780 corrupted files), on-screen and interaction tests, output harness on Vulkan, OpenGL, D3D11/WARP and Metal, quit test on macOS, translations and licence gates, the 3 packages verified on clean machines `[test]`. Rebuilding GraphicsMagick with autotools and MSVC takes 43 min on the Windows runner; the binary cache keeps it afterwards. |
| Next steps | **1.** Release 0.4 (§9): first the corrections from Nuno's review of 0.3 and the dependency audit (D-46), then the D-40 features. **2.** Native review of the translations (D-P13); Nuno reviews European Portuguese. **To do (Nuno):** delete the CI artifacts of runs #2 to #9, which contain x265 (§11, "Deleting old CI artifacts"). **3.** Validate manually on Windows and macOS, including HDR displays and HEIC with Microsoft's extensions installed. **4.** Phase 1: PQ/HLG corpus, pixel value readout, SdrIcc mode, ACM on Windows; D-P10, D-P15. |
| Blockers | None. Before announcing the release: a public-facing README (D-P11). The packages are not signed yet (Phase 4, D-P08). |

**What exists in the code** (`src/`, about 10,200 lines on 2026-10-08):
- a format registry (D-38): 50 formats, detection by the file's first bytes, `--formats`; back ends: OpenImageIO, libjxl, libwebp, our own APNG reader, libheif's track API (AVIF sequences), the system's HEIC decoders (ImageIO, WIC), Qt, and GraphicsMagick 1.3.48 in a sandboxed decode worker process;
- single conversion to linear scRGB in RGBA16F: unbounded LittleCMS, analytic CICP (PQ, HLG, sRGB, BT.1886, γ), OIIO color spaces (including ACES AP0/AP1) and EXR chromaticities;
- EXIF orientation;
- `QWindow` + QRhi window: D3D11 on Windows, Metal on macOS, Vulkan or OpenGL on Linux;
- automatic swapchain selection: scRGB/EDR when the display supports HDR, HDR10, otherwise SDR;
- `hdrInfo` → SDR white scale and display peak;
- shader with SDR, scRGB and PQ outputs; BT.2390 EETF tone mapping or clipping (T key); PQ in absolute nits where the output allows it (D-15); CPU reference of the same stage (`color::applyOutputStage`);
- `--render` harness: 1:1 offscreen render, GPU readback and comparison with the reference (D-16);
- information panel and E14 top overlay at SDR white level (D-34);
- zoom at cursor, exact 100 %, pan, view rotation and mirroring, exposure, altered-pixel warning, checkerboard behind transparency;
- animation playback with pause and frame stepping, and the slideshow (E6, E11);
- folder navigation sorted by name, date or size, preloading of both neighbours (D-33), folder watching; drag and drop; command table and context menu (D-30); settings and session (D-29, D-44); file actions (copy, rename, trash and undo, permanent delete, recent files); `--info`, `--formats`.

**Notes for cloud sessions** (Linux container):
- `download.qt.io` and GitHub *releases* are blocked by the proxy `[test]`. Qt 6.12 is built from GitHub with `scripts/build-qt-linux.sh` (≈15 min with 4 vCPU).
- The image dependencies come from Ubuntu 24.04 apt (OIIO 2.4, lcms 2.14, libjxl 0.7, libheif 1.17). OIIO 2.4 ignores PNG `cICP` (OIIO 3.1 in CI reads it), and libheif 1.17 has no track API: AVIF sequences and the decode worker need libheif ≥ 1.23 and GraphicsMagick 1.3.48 built locally, which `tests/smoke.sh` otherwise skips.
- Workflow: `cmake --preset linux-system && cmake --build --preset linux-system && tests/smoke.sh build/linux-system/imageViewer`.
- The Qt `offscreen` platform does not expose the window, so it does not exercise the renderer. That requires Xvfb with the xcb plugin (the script already builds it).
- Performance measurements are only valid with an idle CPU: a build running in parallel multiplied the measured decode time by 7.

**CI notes** (GitHub Actions, run 1 of 2026-10-06):
- On Windows, aqtinstall 3.3.0 cannot find the Qt 6.11.2 metadata ("Failed to locate XML data"). The workflow uses the development version of aqtinstall in that job only.
- Linux and macOS install Qt 6.11.2 with aqtinstall 3.3.0 without problems.
- On Windows, the development aqtinstall (3.3.1.dev166) finds Qt 6.11.2, but py7zr fails to extract qtsvg. Extraction is now done with the runner's 7-Zip (`--external 7z`).
- **Run 2:**
  - **macOS arm64 green:** build, smoke tests, deploy and artifact.
  - **Linux:** build and smoke tests green. The screen test failed only because numpy was missing from the `setup-python` Python (fixed with `pip`).
- **Run 3:** macOS and Linux green. On Windows, the parallel extractions by the external 7-Zip collided in the same folder. Fix: install only the archives used (`qtbase d3dcompiler_47 opengl32sw`) with the default extractor.
- **Run 4:**
  - **Windows:** the Qt installation passed.
  - **macOS:** green, including `Package` (`.dmg`) and `Upload`.
  - **Linux:** the screen test did not find the image. The viewer log had only the decode line, without the `backend` line: at 6 s the renderer had not started yet `[test]`.
  - **Cause:** the test waited for a fixed time. Locally, the Vulkan window (lavapipe) becomes exact in 0.9–1.3 s; in a cold run it took 11 s `[test]`. On a freshly provisioned runner, Mesa/LLVM startup can take longer than 6 s `[inference]`.
  - **Fix:** `tests/screen_test.py` *polls* the screen until the image appears exact, with a 60 s deadline, and fails immediately if the viewer exits. The log now has per-line timestamps. On failure, CI saves the screenshot and the log as an artifact.
- The 1st build of the vcpkg dependencies is long. The binary cache is saved even if the job fails (`if: always()`; later a partial save, only when the regular save did not happen).
- A new push to the branch cancels the run in progress (concurrency group). During the 1st Windows vcpkg build, it is best to hold back pushes until the job finishes.
- **Run 4, Windows:** the 1st vcpkg build took 34 min. After that, the full Windows job takes about 1.5 min with the cache `[test]`.
- **Run 5 (9711205):** Windows and macOS green with the Phase 1 code, which confirms that it compiles with MSVC and Apple clang. On Linux, the screen test with *polling* passed in 20 s. The `--render` harness failed in every case with "neutral is not neutral": the decoded pixel (0,0) was the last row of the corpus `[test]`.
  - **Cause:** in OIIO 3.1, `PNMInput::open` without a configuration sets `m_pfm_flip = false`; only the variant with a configuration reads `pnm:pfmflip` (default 1) `[code: OIIO v3.1.14.0, pnminput.cpp:494,510; imageinput.cpp:154]`. The local OIIO 2.4 flips by default, which is why it passed locally.
  - **Fix:** `decodeWithOiio` always opens with a `pnm:pfmflip = 1` configuration. The `rows2.pfm` (1×2) smoke test checks orientation and linear reading on all 3 systems.
- The workflow cancels older runs on the same branch. This matters because macOS minutes cost 10× in a private repository `[knowledge]`.

## 2. Decision log

| ID | Date | Decision | Reason |
|---|---|---|---|
| D-01 | 2026-10-06 | ~~Proprietary product~~ → **superseded by D-18**. Direct distribution (no app stores) is kept. | Nuno's decision. |
| D-02 | 2026-10-06 | ~~Python + PySide6~~ → **superseded by D-05** | — |
| D-03 | 2026-10-06 | ~~SDR only in v1~~ → **superseded by D-06** | — |
| D-04 | 2026-10-06 | Reject Magick.NET. | In Python it was viable but dominated; in C++ the direct equivalent would be ImageMagick, not .NET (§4.3). |
| D-05 | 2026-10-06 | **C++20 + Qt 6.11** | Performance, full control over each system's color and HDR APIs, and smaller binaries. It is the path qView took. |
| D-06 | 2026-10-06 | **HDR from the ground up** (PQ, HLG, EXR and float), with real HDR output where the system allows it. | Safe use by industry professionals. |
| D-07 | 2026-10-06 | **Format coverage ≥ qView ∪ ImageGlass ∪ FFmpeg (image2)** | Highest industry standard (Appendix A). |
| D-08 | 2026-10-06 | **Minimal code structure:** one executable and about 10 source files, in a single `CMakeLists.txt`. | Simple code management. |
| D-09 | 2026-10-06 | The viewer is a `QWindow` with its own QRhi swapchain. Secondary dialogs use Widgets. | The Widgets backing store does not support HDR `[code: qtbase 6.11, src/widgets, src/gui/painting]`. Qt Quick HDR can only be enabled through an environment variable (`QSG_RHI_HDR`) `[code: qtdeclarative 6.11, qsgrhisupport.cpp:1542]`. |
| D-10 | 2026-10-06 | The internal working space is **linear scRGB** (BT.709 primaries, extended values, 1.0 = SDR white) in premultiplied RGBA16F textures. | It matches the native output of Windows (scRGB) and macOS (EDR), and represents any color gamut. |
| D-11 | 2026-10-06 | No code is copied from qView or ImageGlass (both GPL-3). They serve only as behavioral references. | Compatibility with the product license (D-18): GPL-3 code would force the whole work to be GPL-3. |
| D-12 | 2026-10-06 | CICP transfer characteristics 1/6/14/15 (BT.709/601/2020) are decoded with the BT.1886 EOTF (γ2.4, black 0), not with the inverse OETF. | Content with these codes is *display-referred* (video); this is the convention of reference players. The alternative is recorded for the Phase 1 tests. |
| D-13 | 2026-10-06 | Distribution binaries are produced **only by CI**; local builds are for development only. ~~A `vX.Y.Z` tag creates a draft *Release*~~ → version format: **D-19**; publishing flow: **D-26**. | It is reproducible, centralized and does not depend on anyone's machine. macOS can only be built on a Mac, and CI has runners for all 3 systems. |
| D-14 | 2026-10-06 | Requirement E14 (configurable fullscreen info overlay) based on Nuno's request to ImageGlass (#2475). | Explicit request; fits into the renderer's SDR overlay without changing the viewport. |
| D-15 | 2026-10-06 | **Output stage** (resolves D-P04). Default tone mapping: **BT.2390 EETF** (blacks at zero), in the PQ domain, applied to max(R,G,B), with all components scaled by the same ratio. It engages only when the content's maximum **luminance** (after exposure) exceeds the output peak. The source peak is the image's actual maximum. T key: "signal" mode (per-component clipping). **PQ** content keeps absolute nits (203 nits per unit) on the PQ and Windows scRGB outputs; SDR and HLG stay relative to SDR white. In SDR and EDR, 1 unit = 203 nits for EETF purposes. | Identity below the knee (H4), and content within the display's range left intact (H2). Scaling by max(R,G,B) preserves hue and never exceeds the peak. Using luminance as the criterion prevents wide-gamut SDR photos (P3 red = 1.22 in BT.709) from being darkened: that is a gamut problem, not a luminance problem. The source-peak statistic remains open (D-P10). |
| D-16 | 2026-10-06 | The output stage is verified with an **offscreen harness** (`--render`): it draws the image 1:1 through the real shader, reads the float target back from the GPU and compares every pixel with `color::applyOutputStage` (CPU reference). `tests/render_test.py` also checks, independently of the C++ code, the properties from the specification (BT.2390 knee computed with the ITU formula, identity, peak, monotonicity, clipping). | Makes H2/H4/H6 verifiable in CI without an HDR display. Comparing only GPU against CPU would not catch a specification error shared by both; the independent properties do. |
| D-17 | 2026-10-06 | **PFM** files (float PNM) are treated as **linear BT.709**, ignoring the `oiio:ColorSpace = "Rec709"` that OIIO (2.4 and 3.1) assigns to every PNM. Rows are always read bottom to top (`pnm:pfmflip`), as the format requires. | PFM has no color metadata and is linear by convention (HDR radiance maps) `[knowledge]`. Decoded as BT.1886, 36.0 became 5434 `[test: tests/render_test.py]`. |
| D-18 | 2026-10-06 | **Apache-2.0 license** (open source). `LICENSE` with the canonical text from apache.org (sha256 `cfc7749b…3d30`, identical to the one in `/usr/share/common-licenses`) `[test]`. `NOTICE` with the Cristallumnis copyright and the trademark reservation (Apache §6). The CI packages include `LICENSE` and `NOTICE` (Apache §4; on macOS in `Contents/Resources`). Contributions with a DCO *sign-off*, no CLA. Cristallumnis AI modules will be separate proprietary plugins (*open core*). The dependency rules (§5) still apply. | Nuno's decision, following the recommendation in D-P01: brand visibility among professionals, an explicit patent grant, and it is the license of the ASWF tools (OIIO, OCIO, OpenRV). No CLA is needed: Apache-2.0 already allows Cristallumnis to use contributions in proprietary products; the DCO guarantees provenance `[knowledge]`. |
| D-19 | 2026-10-06 | **X.Y versions** (0.1, 0.2, … 1.0, 1.1), with an optional suffix (`0.2-alpha`, `1.0-rc.1`); never X.Y.Z. Tags `vX.Y[-suffix]`; a tag with a suffix = pre-release. The version comes from the tag: CI passes `-DIMAGEVIEWER_VERSION=X.Y[-suffix]` and CMake rejects any other format; development builds show `0.1-dev+<commit>`. | Nuno's rule. A single source of truth (the tag) prevents versions from getting out of sync between the tag, the binary and the package. |
| D-20 | 2026-10-06 | `packaging/` directory (exception to D-08): vcpkg *overlays* (ports and triplets), third-party license texts and the CI license checker. It contains no application code. | Needed to control what vcpkg builds and to comply with licenses; keeping this out of `src/` keeps the code minimal. |
| D-21 | 2026-10-06 | **Alpha:** decoders deliver straight alpha; premultiplication happens once, in linear light. **Compositing:** translucent pixels are composited over the background in linear light in the shader (and in the CPU reference), on every output. | Adversarial review: OIIO premultiplied encoded values and we premultiplied again, so an image with alpha 0.5 came out 4.5× darker `[test]`. Compositing in encoded space gave different results in SDR, scRGB and PQ `[test]` (F6). |
| D-22 | 2026-10-06 | **CICP takes priority over ICC** when both are present and the CICP codes are supported. | HDR JPEG XL always carries a synthesized ICC and PNG can carry cICP+iCCP; the ICC is only an SDR approximation of PQ/HLG (PQ at 203 nits gave 0.0203 instead of 1.0) `[test]`. The 3rd edition of the PNG specification gives precedence to cICP `[knowledge]`. |
| D-23 | 2026-10-06 | **Matrix/curve ICC** profiles (almost all camera and display profiles) are evaluated directly: per-channel curves and a matrix (PCS D50, Bradford to D65). LittleCMS is kept when the profile has a LUT or a non-zero black point. | It is 3 to 4 times faster on 24 MP JPEGs and identical to LittleCMS on whole images, to within one FP16 rounding (maximum difference 0.000488, mean 1e-8) `[test]`. `IMAGEVIEWER_ICC_LCMS=1` forces LittleCMS for diagnostics. |
| D-24 | 2026-10-06 | **macOS always uses the extended linear sRGB swapchain** (layer tagged for ColorSync), even on SDR displays, and re-tags it after display changes. The EDR *headroom* is read every frame. | An SDR layer inherits the display's color space, and sRGB reached wide-gamut displays unconverted `[code: qnsview_drawing.mm, qrhimetal.mm]`. Qt warns that the initial *headroom* may be wrong `[doc: qrhi.cpp]`. |
| D-25 | 2026-10-06 | **Libheif without HEVC:** *overlay* without the default feature `hevc` (x265, GPL-2) and without libde265. HEIC no longer opens until there is a decision on D-P03; AVIF still works (aom). CI has a checker that fails if any GPL (non-LGPL) package gets in. | OpenImageIO requested libheif with its default features and overrode the exclusion in our manifest: x265 was in the packages `[test: CI logs, vcpkg ports 434307da]`. |
| D-26 | 2026-10-06 | **Automatic releases:** publishing a `vX.Y[-suffix]` release on GitHub (or pushing the tag) runs the build, verifies the packages on clean machines and attaches the 3 packages and `SHA256SUMS` to the release. Actions are pinned by SHA. | Nuno's request: a fully automatic process. The `release: published` event is documented `[doc: github/docs, events-that-trigger-workflows.md]`; the `.dmg` did not start on a clean Mac (`macdeployqt` did not resolve the vcpkg libraries) and CI did not detect it, so the packages are now tested on a machine without Qt `[test: CI logs, run 9]`. |
| D-27 | 2026-10-07 | **English-only repository, translated interface.** Everything committed is English: code, comments, UI source strings, documentation (this plan became `docs/PLAN.md`), commit messages. The interface is translated with Qt Linguist into the **16 most spoken languages** by total speakers (Ethnologue 2026, 29th edition): English (source), Simplified Chinese, Hindi, Spanish, Arabic, French, Bengali, Portuguese (European), Indonesian, Urdu, Russian, German, Japanese, Marathi, Vietnamese, Telugu. Nigerian Pidgin and Egyptian Arabic (ranks 14 and 15) have no separate UI locale: Nigerian users use English and Egyptian Arabic is served by Arabic, so Vietnamese and Telugu (ranks 17 and 18) complete the list `[knowledge: secondary sources citing Ethnologue 2026; ethnologue.com was not reachable]`. `.ts` files in `translations/` (exception to D-08), compiled into the executable; the language follows the system or the Settings; Arabic and Urdu switch the layout to right to left; numbers follow the UI locale. Translations other than English are **machine-generated and await native review** (the Settings say so); `tests/check_translations.py` checks placeholders on every build and completeness on release tags. Supersedes D-P11 and the "pt-PT and en-US" of E12. | Owner's rule (2026-10-07). An international product (studios, platforms, colour professionals) needs one working language for code and documentation and many for the interface. Qt Linguist is Qt's native tool: no new dependency, `tr()` everywhere, runtime switching. |
| D-28 | 2026-10-07 | **Feature parity with qView, then ImageGlass, in our own design and code.** Everything good in them is offered, never by copying (D-11): behaviour is studied from documentation, UI resource files and release notes, and implemented independently. The inventory and status are in §8.1; release 0.1 takes the first qView set. | Owner's request. Users who switch from either should find what they rely on. |
| D-29 | 2026-10-07 | **Settings and session in `QSettings`** (registry on Windows, plist on macOS, INI on Linux; organization "Cristallumnis", application "imageViewer"). One `Settings` struct with defaults; `load()` validates every value (unknown language → system, colour → default, width clamped, unknown enum → automatic). Session state saved on close: normal window geometry, maximized/full screen, last file, last folder (preferences: when they change, D-44). Tests that start the GUI use their own `XDG_CONFIG_HOME`. | One place for every preference keeps the dialog, the defaults and the stored values consistent; validation keeps a corrupt or hand-edited file from breaking the application. |
| D-30 | 2026-10-07 | **One command table** drives the keyboard and the context menu: each user action is a `ViewerWindow::Command` with its shortcuts, label, enabled/checked state and handler. Context menu grouped like desktop applications: frequent file actions at the top level (Open, Show in folder, Copy image, Copy path, Move to trash), then **View**, **Image**, **Color & HDR**, **Go** submenus, then Settings, **Help**, Quit. `ViewerWindow` is split in `viewer.cpp` (display, view state, input) and `commands.cpp` (commands, menus, file operations, dialogs). Platform wording: "Show in Explorer/Finder/File Manager", "Recycle Bin" on Windows. The macOS menu bar waits until it can be tested on a Mac (native key equivalents could fire commands twice). | Keyboard and menus can no longer drift apart; similar actions are grouped where users look for them; viewer.cpp would have exceeded the D-08 size limit. |
| D-31 | 2026-10-07 | **Clickable side zones** for previous/next image: ~~200~~ 100 logical pixels on each side since 0.3 (D-36) (Settings, 80–400; at most a quarter of the window), a round 56 px button with a chevron shown while hovering, a click (press and release within the platform's drag distance) navigates, a drag still pans, a double-click in a zone keeps navigating; disabled with fewer than two images or at either end when navigation does not loop. Mouse back/forward buttons also navigate. Not mirrored in right-to-left languages (like media controls). | Owner's request (qView has no such zones; ImageGlass shows similar buttons on hover). Each UI layer has its own small texture in the renderer, so hover feedback never re-uploads the information panel. |
| D-32 | 2026-10-07 | **37 interface languages** (resolves D-P12). The 16 of D-27 stay; added: **Korean, Italian, Turkish** (display, streaming and film markets) and every official EU language that was missing: **Bulgarian, Croatian, Czech, Danish, Dutch, Estonian, Finnish, Greek, Hungarian, Irish, Latvian, Lithuanian, Maltese, Polish, Romanian, Slovak, Slovenian, Swedish**. With English, French, German, Italian, Portuguese and Spanish, all 24 official EU languages are covered. The Settings list shows each language by its own name, sorted by its English name (stable whatever the interface language). Plural forms follow Qt Linguist's rules per language. All 36 translations are machine-generated and await native review (D-P13); Irish and Maltese have the least training material behind them, so they are the first candidates for review `[inference]`. | Owner's decision (2026-10-07): keep the work already done, add the market languages and cover the European Union, the first market of a Portuguese company. |
| D-33 | 2026-10-07 | **Preloading of the next and the previous image.** Once the requested image is shown, the decode thread decodes its two neighbours (first in the direction of travel, then the other) into a RAM cache of finished images: linear scRGB RGBA16F, exactly what is uploaded to the GPU, so a step either way skips the decode and only the upload remains. The cache keeps the current image and its two neighbours only, within a budget of a quarter of physical memory (at least 256 MiB, at most 4 GiB). A neighbour whose source exceeds a third of the budget (8 bytes per pixel) is not preloaded; the check uses the header, before any pixel is read. Entries are keyed by path, file size, modification time and texture limit, checked against the file before use, and dropped when the file is renamed, deleted or changed, and on a language change (descriptions are composed while decoding). Cache and renderer share the pixel buffer (`std::shared_ptr`), so nothing is copied. There is still one decode thread: a preload cannot be cancelled, so a jump to a non-neighbouring image can wait for at most one preload. Setting: "Load the next and previous images in advance" (on). `ImageCache` lives in `cache.{h,cpp}`; loading, preloading and folder watching move from viewer.cpp to `navigation.cpp`. | Owner's request: the previous image must also appear at once when navigating backwards. qView preloads ±1 by default, ImageGlass 0–10 (§8.1). Phase 3 target: the next image in ≤ 50 ms. Keeping the current image's pixels in RAM is what makes the step back instant. |
| D-34 | 2026-10-07 | **Information in two places only.** (1) **The information panel (I)** holds every detail, as in 0.1-alpha, in one bottom-corner panel with label and value columns: file (name, folder, size, modified, position in the folder), image (dimensions and megapixels, format, bit depth, alpha, orientation), colour (description of the source, peak), camera when present (make and model, lens, exposure time, aperture, ISO, focal length, date taken), view and output (zoom, rotation and mirroring, exposure, output, graphics backend, tone mapping or clipping). There is no separate file-information dialog. (2) **The top overlay (E14)**: one compact line at the top of the window that never changes the viewport; by default exactly the six fields of ImageGlass issue #2475 (file name, dimensions, file size, zoom, colour space, modification date); position in the folder and output mode are optional fields; fields can be switched off and reordered; visibility set separately for full screen (default: show on hover at the top, hide after a delay) and for the window (default: hidden); background opacity, text opacity and text outline; Shift+I shows or hides it. The drawing code of `ViewerWindow` moves to `overlays.cpp` (viewer.cpp had passed the D-08 limit at 905 lines). | Owner's request (2026-10-07): no information scattered across the application; a quick glance at the top, with only what the ImageGlass request asked for. |
| D-35 | 2026-10-07 | **Full adversarial review before every pull request**, of the whole codebase and not only the changes, for bugs, security, performance and structure. The procedure is in §11 ("Before every pull request"); a PR is not opened while a confirmed bug or security finding is open. | Owner's rule. The review before 0.1-alpha found defects that tests and diff reviews had missed: alpha 4.5× too dark, x265 (GPL-2) inside the packages, an unlaunchable `.dmg` (D-21, D-25, D-26). |
| D-36 | 2026-10-08 | **Side zones 100 px by default; Apply in the Settings.** The side click zones default to 100 logical pixels (was 200, D-31; range 80–400 unchanged). Up to 0.2 every save stored the default like a choice, so a stored 200 in settings of version < 2 is read as the new default; the settings `version` key describes the preferences and is written only by `Settings::save()`. The Settings dialog has **Apply** next to OK and Cancel: it applies without closing, is enabled only while the dialog shows values the viewer does not use yet, OK applies what is left, Cancel keeps what Apply applied (the convention of Windows and KDE dialogs). A language applied this way rebuilds the dialog in that language, keeping the open tab and any change not applied yet. | Owner's request (2026-10-08): narrower margins leave more of the image clickable for panning, and settings can be tried without reopening the dialog. |
| D-37 | 2026-10-08 | **Who writes the code.** Application code, tests and build scripts are written by the main session's model, never delegated to a less capable model; parallel reviewers (D-35) run on the same model. Less capable models may only translate the interface (`translations/*.ts`), and their output still passes `tests/check_translations.py`. | Owner's rule (2026-10-08): code written by weaker models degrades the codebase. |
| D-38 | 2026-10-08 | **Formats: one registry, detection by content, hybrid isolation** (resolves D-P05). (1) One table of formats (`formats.{h,cpp}`): extensions, MIME type, signature, decoder, capabilities (HDR, alpha, animation, pages), library and licence, test file. It drives detection, the Open dialog filter, `--formats`, the README table, the file associations (Phase 4) and a CI check that every format has a test file decoded on the 3 systems. (2) The file's first bytes choose the decoder; the extension only decides between candidates and for formats without a signature (TGA). (3) Specialist libraries, fuzzed continuously upstream (OSS-Fuzz), run in the application: libjpeg-turbo, libpng, libwebp, libjxl, libheif with aom, OpenEXR, libtiff, OpenJPEG, LibRaw, OpenImageIO. (4) The long tail goes to **GraphicsMagick 1.3.48** (MIT, released 2026-07-23 `[test: NEWS.txt of the release tarball]`), only inside a separate **decode worker** process (the same executable started with `--decode-worker`, D-08): an allow-list of coders (no delegates or external programs, no pseudo-formats such as MSL, MVG or TXT), pixel, memory and time limits, the worker killed on timeout; a crash there means "cannot open this file", never a crash of the viewer. (5) A fuzz smoke test in CI: every test file corrupted in many ways must decode or fail cleanly, within a time limit. | Owner's request (2026-10-08): as many formats as possible, safely and uniformly. ImageMagick, the engine behind Magick.NET, was the alternative: more formats, but no vcpkg port (our own build on 3 systems), delegates under GPL/AGPL (Ghostscript, jbigkit, FFTW) and a larger attack surface `[knowledge]`. The owner accepted GraphicsMagick on condition of its latest release; vcpkg's baseline has 1.3.45 (2024), so an overlay port carries 1.3.48. |
| D-39 | 2026-10-08 | **HEIC through the operating system** (resolves D-P03). macOS: ImageIO (licensed by Apple). Windows: WIC with Microsoft's HEIF and HEVC Video Extensions when installed. Linux: no HEVC decoder ships; the distribution's own libheif with libde265 may be used later. When the system cannot decode a HEIC file, the message says how to add HEVC support (the Microsoft Store extension on Windows). No HEVC decoder is distributed in our packages. | HEVC patents (R4): the system's decoders are already licensed; the owner wants a clear note for the user rather than a silent failure. x265 (owner's question, 2026-10-08) is an HEVC *encoder* under GPL-2.0-or-later `[code: vcpkg ports/x265 4.3]`: it cannot decode HEIC and cannot ship (D-25). |
| D-40 | 2026-10-08 | **Release 0.3 = formats first.** Registry and detection by content (D-38), JPEG XL (libjxl), animation of GIF, WebP, APNG, JPEG XL and AVIF sequences with pause and frame stepping (E6), slideshow (E11), HEIC through the system (D-39), the decode worker with GraphicsMagick (D-38), the fuzz smoke test. Window matching the image, zoom modes, title bar modes, shortcut editor and Open With move to 0.4. | Owner's decision (2026-10-08): JPEG XL files did not open and animated WebP stayed still in 0.2; format coverage is the product's second differentiator (§3). |
| D-41 | 2026-10-08 | **Files for 0.3** (D-08 size rule). `codecs.cpp`: the back ends that parse from memory (libjxl, libwebp; Softimage PIC since D-45); `apng.cpp`: APNG, split from `codecs.cpp` when it reached 859 lines (0.3 review); `decoders.cpp`: OpenImageIO, Qt, detection and the helpers they share; `files.cpp`: the file operations of `ViewerWindow` (show, copy, rename, trash and undo, delete), out of `commands.cpp`, which had reached 809 lines; `playback.cpp`: animation playback and the slideshow; `heif.cpp`: AVIF sequences (libheif's track API); `syscodecs.cpp`: the system's HEIC decoders (platform code, CoreGraphics and COM, kept apart from the portable back ends); `worker.cpp`: the decode worker (both sides of the protocol, the only file that includes GraphicsMagick). | `decoders.cpp` would have passed 1,000 lines, `commands.cpp` 850 and `codecs.cpp` (850) 1,100 with AVIF; each new file holds one concern. |
| D-42 | 2026-10-08 | **Decoding and output conventions fixed by the 0.3 review (D-35).** (1) **GIF loops as browsers play them** (Chrome 65+, Firefox 59+): no NETSCAPE block plays once, a count N plays N + 1 times, 0 forever `[doc: trac.webkit.org/r230712, bugs.webkit.org/show_bug.cgi?id=39857]`. (2) **Premultiplied sources:** only OpenEXR and TIFF with associated alpha are divided by alpha (D-21); every other OpenImageIO reader delivers straight alpha whether or not it sets `oiio:UnassociatedAlpha` (BMP, DDS, ICO, JPEG 2000 and SGI do not set it). OpenImageIO's GIF reader reports an `alpha_channel` past the last channel: the channel named "A" is used. (3) **The clip at the output peak** happens in BT.2020 on the scRGB/EDR and PQ outputs and in BT.709 on SDR, in the shader and in `applyOutputStage` alike. (4) **The source peak** (D-15) is what can reach the screen: a·c + (1 − a)·min(c, 1), the pixel over the brightest background; an animation raises it to its brightest frame shown so far and never lowers it during playback. (5) **Lossy (XYB) JPEG XL** is decoded to float, which keeps colours outside the target gamut. | Each was a confirmed defect `[test]`: a transparent GIF shown opaque; an orange at alpha 0.5 in BMP or SGI shown as (1, 0.57, 0.13) instead of (0.58, 0.13, 0.03) because its straight alpha was divided again; every loop count one play short; a Display P3 red clipped to 1.0 on EDR without headroom (1.2246 now); one EXR edge pixel at alpha 0.001 setting the peak to 5× SDR white (1.0004× now); the later, brighter frames of an HDR animation clipped (a 203-nit and a 1000-nit half both at 255 in SDR; 229 and 255 now); a ROMM orange in lossy JPEG XL read as (1, 0, 0.008) instead of (1.25, −0.056, 0.0085). |
| D-43 | 2026-10-08 | **The decode worker, hardened** (completes D-38 point 4). GraphicsMagick is configured with `--disable-installed`, and the worker refuses to decode while any delegate with a command line exists (the built-in list only holds an empty placeholder). The coder module path, `MAGICK_TMPDIR`, `TMPDIR`, `TMP`, `TEMP` and `HOME` point to the worker's empty private directory, so temporary files land there and vanish with it, also after a kill. The pixel limit also respects the viewer's decode memory budget (60 % of physical memory), and an answer announcing more than fits is refused before its data is read. Standard error is always drained. The fuzz smoke test counts a crash, a hang or a malformed answer of the worker as a failure. | The review reproduced, with the 0.3 worker: a delegate started through a DICOM file carrying a JPEG, temporary files left by a killed worker, and a crafted file taking 2.2 GB (229 MiB now) `[test]`. |
| D-44 | 2026-10-08 | **Several instances share the preferences.** A preference changed outside the Settings dialog (information panel, top overlay, checkerboard, "do not ask again") is written into the stored preferences when it changes, not by saving the instance's whole copy; preferences are no longer saved at exit (the session still is, D-29); the recent files are edited on the stored list, so the Open Recent menu shows every instance's files; a settings file of an older version is migrated and written back at startup. The Settings dialog still applies everything it shows. | A second window overwrote the first one's changes when it closed: a checkerboard switched on in one was off again, and a file opened in one vanished from the recent files when the other trashed an image `[test]`. |
| D-45 | 2026-10-08 | **OpenImageIO readers that crash on damaged files are kept away from them.** Softimage PIC is read by our own reader (`codecs.cpp`: the 104-byte header, chained channel packets, raw, run-length and mixed run-length scanlines, every read checked); Maya IFF is read through OpenImageIO only from 3.0, whose reader checks its spans. Such a format never reaches OpenImageIO, not even as the fallback of a failed decode (`oiioMayRead()`); with an older OpenImageIO, IFF is listed as unavailable. | The fuzz smoke test's first CI run (58) crashed the viewer on a truncated PIC on Linux and macOS: OpenImageIO 2.4.17 and 3.1.14 read on through the file they had closed (`fgetpos` on a null `FILE*`, gdb) `[test]`. OpenImageIO 2.4 also reads raw PIC packets as black and aborts on pure run-length ones; our reader shows all three encodings exactly on screen, and agrees with OpenImageIO on the mixed one `[test]`. The 2.4.17 IFF reader writes past its tile buffers after one flipped bit (valgrind); 3.1.14 rewrote it with checked spans and passes the same files `[code: OIIO v3.1.14.0 iffinput.cpp]`, `[test]`. |
| D-46 | 2026-10-08 | **Dependencies at the state of the art.** Every library we build or ship (vcpkg ports and our overlays, Qt and the Qt version CI installs) is at its latest stable release. The check runs at the start of each release's development and again before its PR (§11): a table of each dependency's version against upstream's latest, with the date. A release that fixes a security issue, or a bug we can reach, is adopted at once, between releases if needed. Staying behind needs a recorded reason (a regression or incompatibility in the new release, with its reference), revisited at the next check; pre-releases are not adopted. | Owner's rule (2026-10-08): no shipped code with bugs already fixed upstream. vcpkg's baseline lags upstream (GraphicsMagick 1.3.45 there, 1.3.48 upstream, D-38), so the baseline alone is not the check; the overlays (libheif, GraphicsMagick) are checked by hand. |
| D-47 | 2026-10-08 | **GraphicsMagick stays; ImageMagick 7 is the prepared replacement.** The library stays behind one file (`worker.cpp`) and a library-independent protocol (the file's bytes in; pixels, ICC profile and orientation out), so replacing it changes neither the viewer nor the tests. Migration starts when any of these holds: no GraphicsMagick release for 12 months; a published security issue in one of our 16 coders left unfixed upstream for 90 days; a format we need that only ImageMagick reads. A migration keeps the D-43 hardening: the same allow-list of coders, no delegates, resource limits, the fuzz test. | Owner's concern (2026-10-08): the team has contributed to ImageMagick, and GraphicsMagick (forked from ImageMagick 5.5.2 in 2002) has essentially one maintainer `[knowledge]`. It is maintained today: 1.3.47 in May 2026, 1.3.48 on 2026-07-23 and 1.3.49 on 2026-10-08 `[doc: Wikipedia]`, `[test: NEWS.txt of the release tarballs]`; the 1.3.49 notes ask for volunteers because "the burden has entirely been on me" `[doc: NEWS.txt]`. The D-38 reasons hold for now. |
| D-48 | 2026-10-09 | **Keyboard convention.** Single keys for viewing and navigating; **Shift** + a key for that key's second command, its reverse or its alternative (Shift+R, Shift+E, Shift+I, Shift+← and Shift+→ for the first and last image, Shift+Delete); **Cmd** on macOS, Ctrl elsewhere, for the platforms' application commands (open, copy, undo, settings, quit, move to trash on macOS, zoom) and for resets (Cmd+E exposure, Cmd+0 fit); never Alt/Option. A key that a MacBook reaches only with fn (Home, End, Page Up and Down, the F keys, forward Delete) is never a command's only shortcut, and menus show macOS's own shortcut first. So: first/last image Shift+←/→ (Home/End kept); Rename also Return (Finder's key); Delete Permanently also Cmd+Shift+Backspace; Move to Trash shows ⌘⌫ on macOS. | Owner's rule (2026-10-09): few kinds of key combinations, Shift for alternatives, Cmd for copy, undo and the like. On a MacBook, Home and End need fn, and macOS menus show them as ↖ and ↘, which read as a key combination. |
| D-49 | 2026-10-09 | **Information panels: one look, legible over any image.** The panel (I) and the top overlay (Shift+I) share background opacity (default 70 %), text opacity and the text outline (default on): values grey 240, labels 190 (`Settings::panelBackground()`, `panelText()`). UI overlays blend as in SDR in every output: in linear outputs (scRGB, EDR) the shader turns the weight of what lies underneath into (1 − a)^2.2, and in PQ into the weight that darkens SDR white as much, so the same setting looks the same in SDR, EDR/scRGB and HDR10. Labels keep 4.5:1 (WCAG AA) over SDR white, and against their outline over the brightest content the output shows; `imageViewer --panel-check` measures it, run by `render_test.py` in every output. Settings saved by 0.3 move from 60 % without outline to the new defaults. | Owner's report (2026-10-08, screenshot on a MacBook in EDR): the labels vanished over a light image. Reproduced with the previous shader: label contrast 1.08:1 in EDR, 2.2:1 in SDR and 5.9:1 in PQ, and the background over white at 0.41, 0.14 and 0.02 of SDR white: one opacity looked different in every output `[test: --panel-check]`. Now 4.6 to 4.7:1 in all four outputs, 11:1 against the outline over HDR highlights `[test]`. |
| D-50 | 2026-10-09 | **Qt 6.12; macOS 14.4 or later** (resolves D-P07 for macOS). The build and every vcpkg dependency target macOS 14.4. | Qt 6.12.0 is the latest stable release (D-46), and its minimum macOS is 14.4 (Qt 6.11: 13) `[code: qtbase .cmake.conf, QT_SUPPORTED_MIN_MACOS_VERSION]`. The private QRhi API builds unchanged, and every local test passes on 6.12 `[test]`. macOS 13 no longer receives Apple's security updates `[knowledge]`. Reversible: staying on Qt 6.11 keeps macOS 13 at the cost of D-46. |

---

## 3. Vision and scope

imageViewer is a cross-platform image viewer (Windows, macOS, Linux). It is minimalist and fast, and has **demonstrable image fidelity in SDR and HDR**.

ImageGlass 10 is already cross-platform and reads more than 90 formats `[doc]`. What sets us apart from it comes down to three things:
1. a correct and verifiable color and HDR pipeline;
2. professional-grade format coverage (EXR, DPX, RAW, PQ, HLG);
3. a clutter-free interface.

**Essential in v1:**
- **E1. Open:** file associations, `argv` and `QFileOpenEvent`, dialog, drag and drop, recent files.
- **E2. Folder navigation:** natural sorting, wrap-around, *file watcher*, preloading with a memory limit.
- **E3. Formats:** all those in the Appendix A matrix marked as v1.
- **E4. SDR and HDR fidelity:** criteria F1 to F12 and H1 to H6 (§7).
- **E5. Zoom and pan:** fit, fill, 100 % in actual pixels; zoom at cursor; trackpad; view rotation and mirroring.
- **E6. Animation:** GIF, WebP, APNG, AVIF, JXL; play/pause and frame stepping.
- **E7. Multi-image:** multipage TIFF, multi-part/layered EXR, composite PSD.
- **E8. Info panel:** color chain, HDR metadata (MaxCLL, MaxFALL, *mastering display*), essential EXIF.
- **E9. Minimal professional tools:** pixel value as code value and in nits, clipping warning (pixels above the display peak) and exposure for linear content.
- **E10. File actions:** move to trash with undo, rename, copy image/path, show in folder, open with.
- **E11. Fullscreen and slideshow.**
- **E12. Integration:** associations and single instance on all 3 systems; minimal settings; automatic theme; interface in 37 languages (D-27, D-32).
- **E13. Distribution:** packages for the 3 systems produced in CI, with signing optional but recommended.
- **E14. Configurable info overlay** (D-14, origin: [ImageGlass #2475](https://github.com/d2phap/ImageGlass/issues/2475), opened by Nuno on 2026-10-03; in ImageGlass it is labeled *feature*/*ready* and planned for v10.1 `[doc: issue page, read on 2026-10-06 through a tool that summarizes the content]`). Nuno reports that ImageGlass has since implemented it; on 2026-10-07 the issue page still showed it open and the release notes up to v10.0.6.906 (2026-09-05) did not mention it `[doc]`, so imageViewer follows the issue text, not their implementation (D-11).
  - **Behavior:**
    - compact information at the top of the image in fullscreen, drawn on top;
    - reserves no space, and the viewport does not change when the overlay appears or disappears;
    - keeps zoom, pan and scaling mode;
    - updates when the image or any value changes.
  - **Visibility:** always visible · hidden · show on mouse hover and auto-hide (activation area at the top of the screen).
  - **Appearance:**
    - adjustable background opacity, including fully transparent;
    - text opacity independent of the background's;
    - optional text shadow or outline.
  - **Configurable fields:**
    - file name with extension;
    - native dimensions (width × height);
    - file size (KB/MB);
    - zoom in %;
    - profile or color space;
    - modification date.
  - **Our extensions:**
    - same mechanism also in windowed mode (optional);
    - additional fields: SDR/HDR output mode and peak, and position in the folder;
    - the text is composited at SDR white level, so it is never glaring in HDR.

**Optional after v1, in order of value:**
- **O1.** Gain maps (ISO 21496-1 / UltraHDR / Apple), if they do not make it into Phase 5.
- **O2.** Thumbnail strip.
- **O3.** A/B comparison (synergy with Cristallumnis upscaling, as a plugin outside the core).
- **O4.** SDI output to a reference monitor (Blackmagic DeckLink SDK).
- **O5.** Frames from video containers (MOV/MXF) via libavformat.
- **O6.** Remappable shortcuts.

**Out of scope:** image editing and saving, printing, wallpapers, cloud, plugin SDK, telemetry.

---

## 4. References analyzed (summary, still valid)

### 4.1 qView
Basis: jurplel/qView, commit `c5eca1c`, 2026-04-04, v7.0, C++/Qt `[code]`.

**Keep:**
- Minimalism by default: no menu bar, `#212121` background, window fitted to the image at between 20 % and 70 % of the screen.
- Background decoding and preloading of neighbors (memory-bounded cache, with the target profile in the key).
- Two-stage scaling: fast transform during zoom, then high-quality resampling at physical resolution.
- Cursor-anchored zoom; natural sorting; wrap-around in the folder.
- Monitor ICC profile obtained per system: `GetICMProfileW`, `NSWindow.colorSpace`, `_ICC_PROFILE`.
- Non-intrusive associations (`OpenWithProgids`; `CFBundleDocumentTypes` with the *Viewer* role; `.desktop` with `MimeType`); handling of `QFileOpenEvent`.

**Simplify:**
- 6 sort modes become 3.
- 6 dialogs become one info panel and one settings window.
- Opening by URL and update checking are dropped.

**Improve:**
- Converts to 8-bit ARGB32 **before** color conversion (`qvimagecore.cpp`, `readFile`).
- On Linux it only supports X11.
- Decodes animations twice.
- Rotates by re-rendering pixels ("extremely inefficient").
- Rasterizes SVG only once.
- Modern formats depend on kimageformats binaries downloaded during the build.

### 4.2 ImageGlass 10
Basis: d2phap/ImageGlass, commit `2cf91de`, 2026-09-27 `[code]`. Stack: .NET 10 + Avalonia/Skia + Magick.NET Q16-HDRI. Platforms: Windows, macOS 14+ arm64 only, Linux X11 only `[doc]`. License: Classic GPLv3 plus paid Pro editions `[doc]`.

**Keep:**
- Codec registry with priorities: SVG → Skia → Magick as last resort, with detection by file content (`CodecRegistry.cs`).
- High bit depth on the fast path.
- Custom ICC profile.
- BT.2408 HDR→SDR rendering with reference white at 203 nits (`HdrToneMappingOptions.cs`).
- Zoom modes, checkerboard background, per-channel view, reload when the file changes.
- "Set as default" via `ms-settings:defaultapps`.
- Packaging assets: Info.plist, `.desktop` with RAW MIME types, MSI, AppImage, Flatpak.

**Simplify:**
- About 150 commands and `IG_*` brushes become ~35 actions.
- 31 settings pages become 4 sections.
- Editing, themes, plugins and printing are dropped.

**Improve:**
- Color management is turned off for non-"professional" users (`QuickSetupWindow.cs:218`).
- Only obtains the monitor profile on Windows (`Win32ColorProfileProvider`; there is no source on macOS or Linux).
- HDR is only *tone mapping* to SDR, with no real HDR output.
- The ImageMagick fallback is about 3 times slower at decoding than libvips `[test]`.

### 4.3 Magick.NET (D-04)
**Verified:**
- The README states "over 100 major file formats"; the "90+" figure is ImageGlass's `[doc]`.
- Q8, Q16 and Q16-HDRI variants exist `[doc]`.
- Version 14.17.2 loads from Python via pythonnet and .NET 8: ImageMagick 7.1.2-32 Q16-HDRI, 257 readable entries `[test]`.
- Cost in Python: 372 ms extra at startup and reading 3 to 4 times slower than libvips `[test]`.
- The main-branch README states that the next version supports macOS on arm64 only `[doc]`.

**In C++:** if the ImageMagick engine is needed, it is used directly, without .NET. The equivalent MIT-licensed alternative is GraphicsMagick (§5, D-P05).

---

## 5. Stack and dependencies

**Version audit (D-46), 2026-10-09:** every dependency against its upstream's latest stable release `[test: git ls-remote of each upstream repository, SourceForge file lists, vcpkg master 0699a19d]`. vcpkg's baseline lags upstream, so newer releases come in as overlay ports (`packaging/vcpkg/ports`), fetched by git at the release tag's commit. An overlay is dropped once vcpkg's port reaches the same version.

| Component | In use | Upstream latest | License | Role |
|---|---|---|---|---|
| C++20, CMake ≥ 3.24, Ninja | — | — | — | Build |
| **Qt** (Core, Gui/QRhi, Widgets, ShaderTools) | 6.12.0 (D-50) | 6.12.0 | LGPLv3 (dynamic linking) | UI, GPU rendering (D3D11/12, Metal, Vulkan, OpenGL) |
| **OpenImageIO** (overlay; vcpkg has 3.1.14.0) | 3.2.1.1 | 3.2.1.1 (2026-10-02; 3.1 branch at 3.1.18.1) | Apache-2.0 | Main decoder (features in `vcpkg.json`: gif, libheif, libraw, openjpeg, webp) |
| ↳ OpenColorIO (required by OpenImageIO; our own use comes in Phase 5) | 2.6.0 | 2.6.0 | BSD-3 | Named colour spaces |
| ↳ expat (overlay; vcpkg has 2.8.5) | 2.9.0 | 2.9.0 (2026-10-05, CVE-2026-102633, CVE-2026-77214) | MIT | XML for OpenColorIO |
| ↳ minizip-ng (overlay; vcpkg has 4.1.0) | 4.2.2 | 4.2.2 | Zlib | Config archives for OpenColorIO |
| ↳ libheif (overlay) / aom | 1.23.6 / 3.15.1 | 1.23.6 / not checked (aomedia.googlesource.com is blocked from the cloud session) | LGPL-3 / BSD-2 | AVIF (HEIC goes through the operating system, D-39) |
| ↳ LibRaw | 0.22.2 | 0.22.2 | LGPL-2.1 or CDDL | RAW |
| ↳ OpenEXR / Imath | 3.5.2 / 3.2.3 | 3.5.2 / 3.2.3 | BSD-3 | EXR |
| ↳ libtiff, libpng, libjpeg-turbo, OpenJPEG, giflib | 4.7.2, 1.6.59, 3.2.0, 2.5.4, 6.1.3 | the same | permissive | TIFF, PNG, JPEG, JPEG 2000, GIF |
| ↳ highway, brotli, zlib, libdeflate, zstd, liblzma, fmt, pugixml, yaml-cpp, pystring | vcpkg baseline | the same | permissive | Support libraries |
| **libjxl** (used directly since 0.3) | 0.12.0 | 0.12.0 | BSD-3 | JPEG XL, stills and animation |
| **libwebp** (used directly since 0.3) | 1.6.0 | 1.6.0 | BSD-3 | WebP with its ICC profile, animation |
| **Little CMS** | 2.19.1 | 2.19.1 | MIT | ICC → linear scRGB; display 3D LUT |
| **GraphicsMagick** (D-38, overlay) | 1.3.49 | 1.3.49 (2026-10-08: MAT, PICT and WPG security fixes) | MIT | Long tail, only in the decode worker process |
| **FFmpeg** (planned; avcodec, avformat, swscale, avutil, **without** `gpl` or `nonfree`) | — | — | LGPL-2.1+ | Long-tail image2 formats, gifv/mjpeg |
| **lunasvg** (planned; SVG is read by Qt meanwhile) | — | — | MIT | SVG/SVGZ |

**Excluded licenses:**
- exiv2 (GPL-2): metadata comes from OIIO.
- FFmpeg's `gpl` and `nonfree` features.
- x265, including libheif's **default** `hevc` feature in vcpkg `[test: vcpkg ports/libheif]`; libde265 is left out too. HEIC is decoded by the operating system (D-39).
- lcms `fastfloat` and `threaded` plugins (GPL-3.0) `[test: vcpkg ports/lcms]`.
- LibRaw GPL packs.
- Ghostscript (AGPL).

**Linking.** **Dynamic** vcpkg triplets on all platforms (`x64-windows`, `arm64-osx-dynamic`, `x64-linux-dynamic`) to comply with the LGPL. Qt comes as official dynamic binaries (aqtinstall/install-qt-action) in CI.

---

## 6. Architecture

### 6.1 Repository structure (D-08)

```
CMakeLists.txt        a single target: imageviewer
CMakePresets.json     per-OS presets (vcpkg toolchain)
vcpkg.json            dependencies (manifest)
LICENSE, NOTICE       Apache-2.0 and copyright/trademark notice (D-18)
CLAUDE.md             entry point for new sessions -> reads this plan
docs/PLAN.md          this document
src/
  main.cpp            QApplication, arguments, language, session restore, QFileOpenEvent
  viewer.h/.cpp       ViewerWindow (QWindow): window, rendering, zoom/pan/rotation, input
  navigation.cpp      ViewerWindow: folder, loading, preloading, folder watching (D-33)
  overlays.cpp        ViewerWindow: information panel, top overlay, navigation buttons (D-34)
  commands.cpp        ViewerWindow: command table, context menu, dialogs (D-30)
  files.cpp           ViewerWindow: file operations — show, copy, rename, trash and undo, delete (D-41)
  playback.cpp        ViewerWindow: animation playback and slideshow (E6, E11, D-41)
  cache.h/.cpp        preload cache of decoded images (D-33)
  settings.h/.cpp     Settings + session (QSettings), Settings dialog, UI languages (D-27, D-29)
  renderer.h/.cpp     QRhi: SDR/HDR swapchain, textures, pipeline, output modes
  image.h/.cpp        Image + decodeImage(): the single conversion to linear scRGB, orientation, downscale
  formats.h/.cpp      format registry (D-38): signatures, decoders, capabilities, test files, --formats
  decoders.h/.cpp     back ends (D-38): OpenImageIO, Qt, frame readers; detection and fallbacks
  codecs.cpp          back ends that parse from memory: libjxl, libwebp, Softimage PIC (D-41, D-45)
  apng.cpp            animated PNG: frames rebuilt as PNGs and composited (D-41)
  heif.cpp            AVIF image sequences through libheif's track API (D-41)
  syscodecs.cpp       the operating system's decoders: HEIC through ImageIO and WIC (D-39, D-41)
  worker.cpp          the decode worker process: GraphicsMagick for the long tail, and its client (D-38, D-41)
  color.h/.cpp        ICC (lcms2), CICP (PQ/HLG/sRGB/...), matrices, display 3D LUT
  folder.h/.cpp       listing and sorting (natural name order, date, size)
  platform.h          per-OS services (display ICC profile, HDR state, show in folder)
  platform_win.cpp | platform_mac.mm | platform_linux.cpp
  shaders/image.vert, image.frag   compiled with qsb at build time
resources/            icons, Info.plist, .desktop, .rc, .iss
scripts/              build-qt-linux.sh (Qt from source, for cloud sessions)
packaging/            (D-20) vcpkg/ports and vcpkg/triplets (overlays), licenses/ (third-party texts),
                      check_licences.py (CI license gate)
translations/         imageviewer_<lang>.ts, Qt Linguist (D-27), compiled into the executable
tests/                smoke.sh, fuzz_smoke.py, render_test.py, screen_test.py, ui_test.py,
                      animation_test.py, quit_test.py (macOS), check_translations.py, xvfb.py,
                      longtail_data.py (test files built from their specifications); data/ (minimal corpus)
.github/workflows/build.yml   CI: Windows, macOS, Linux -> verified packages -> release (D-26)
```

Actual state: `platform*`, `resources/` and the FFmpeg and lunasvg back ends do not exist yet (SVG is read by Qt, in the graphical interface only); the tree above is the target.

Files are split only when they exceed about 800 lines.

### 6.2 Image pipeline

1. **Format identification** by file content, through the registry (D-38): the first bytes pick the format, the extension only decides between candidates and for formats without a signature (TGA). The format names its decoder: a specialist library (libjxl, libwebp, our APNG reader, libheif for AVIF sequences), OpenImageIO, Qt, the operating system (HEIC, D-39) or GraphicsMagick in the decode worker. That decoder runs first, then OpenImageIO and Qt as fallbacks, which are also all a file of no known format gets.
2. **Decoding at native depth:** uint8, uint16, half or float, with straight alpha.

   The decoder also returns a color descriptor, in order of priority:
   - **a)** CICP (nclx in HEIF/AVIF, `cICP` in PNG, enumerated encoding in JXL), when the codes are supported (D-22);
   - **b)** embedded ICC;
   - **c)** format attributes (EXR `chromaticities`, validated; `oiio:ColorSpace`; PNG `gAMA` gamma);
   - **d)** none: sRGB (integer) or linear BT.709 (float) is assumed, and the interface flags it.

   Alpha always arrives straight (not premultiplied): OIIO is asked for `oiio:UnassociatedAlpha`, and the sources that are premultiplied, OpenEXR and TIFF with associated alpha, are divided by alpha before the transfer curve (D-21, D-42).

   It also returns the orientation and the HDR metadata (MDCV, CLLI and, later, gain map).
3. **Orientation** applied exactly once.
4. **CPU conversion** (worker thread) to **linear scRGB in float**:
   - ICC: lcms2 with a float transform without clamping (*unbounded*), relative colorimetric intent with BPC.
   - CICP: analytic formulas, with ST 2084 PQ in absolute nits and BT.2100 HLG with a peak-dependent OOTF.
   - EXR: matrix derived from `chromaticities`.

   This is followed by premultiplication and packing into RGBA16F.
5. **Upload** to an RGBA16F texture with mipmaps. Images larger than the GPU limit (usually 16384) are reduced with a box filter in linear light; tiling them is Phase 2.
6. **Single shader** (`src/shaders/image.frag`; CPU reference: `color::applyOutputStage`, which must stay identical): sampling → exposure → scaling to output units (relative to SDR white, or absolute for PQ: D-15) → tone mapping (BT.2390 EETF on max(R,G,B), only if the content luminance exceeds the output peak) or clipping (in BT.2020 on the wide outputs, D-42) → output mode:

   | Mode | When | Encoding |
   |---|---|---|
   | `ScRGB` | macOS (always: EDR, surface tagged by Qt) · Windows with Advanced Color active (HDR or ACM) · Linux Vulkan with `EXTENDED_SRGB_LINEAR` | linear, 1.0 = SDR white (macOS) or 80 nits (Windows: SDR white comes from `sdrWhiteLevel`) |
   | `PQ` | HDR10 swapchain (Vulkan/Linux, or an option on Windows) | BT.2020 + ST 2084, absolute nits |
   | `SdrIcc` | Windows without Advanced Color · Linux X11 or SDR | 3D LUT (lcms2: scRGB → display ICC profile) |

7. **UI overlays:** painted with `QPainter` into an SDR RGBA8 texture and composited at SDR white level. The UI is never glaring in HDR.

**Facts that underpin the pipeline `[code: qtbase 6.11]`:**
- `QRhiSwapChain` offers `SDR`, `HDR10`, `HDRExtendedSrgbLinear` and `HDRExtendedDisplayP3Linear`.
- `hdrInfo()` returns:
  - min/max nits on D3D and on Vulkan over DXGI;
  - `maxColorComponentValue` / `potentialEDRHeadroom` on Metal;
  - `sdrWhiteLevel`.
- On Metal, the `CAMetalLayer` is tagged with `ExtendedLinearSRGB`, `ITUR_2100_PQ` or `ExtendedLinearDisplayP3`, and ColorSync does the *matching* to the display.
- On D3D11 the DXGI color spaces `G22_P709` (SDR), `G10_P709` (scRGB) and `G2084_P2020` (HDR10) are used.
- The Vulkan color spaces are `EXTENDED_SRGB_LINEAR`, `HDR10_ST2084` and `DISPLAY_P3_LINEAR`.
- 3D textures and RGBA16F/32F textures are available.

### 6.3 Per-system layer (`platform_*`)

| Service | Windows | macOS | Linux |
|---|---|---|---|
| Display ICC profile (SdrIcc mode) | `GetICMProfileW` per monitor | Not needed (ColorSync) | `_ICC_PROFILE[_n]` (X11), colord |
| Advanced Color / HDR state | DXGI `IDXGIOutput6::GetDesc1` and DisplayConfig (ACM) | `hdrInfo` (EDR headroom) | Vulkan swapchain capabilities |
| Opening via file association | `argv` | `QFileOpenEvent` | `argv` |
| Trash | `QFile::moveToTrash` (all 3) | — | — |
| Show in folder | `explorer /select,` | `NSWorkspace activateFileViewerSelecting` | D-Bus FileManager1 `ShowItems` |
| Single instance | `QLocalServer` | System (Apple Events) | `QLocalServer` |

### 6.4 Format integration policy

1. **One path per format, from the registry (D-38):** the file's signature picks the decoder (specialist library → OpenImageIO → Qt → GraphicsMagick in the worker). A second decoder is not added for a format that is already covered without a demonstrated defect in the first one.
2. **Dependencies only through `vcpkg.json`,** with a pinned baseline. The version is bumped deliberately and the change goes through CI.
3. **License:** only LGPL (dynamic linking) or permissive. Before enabling any vcpkg feature, its license is checked: libheif's default `hevc` feature pulls in x265, which is GPL.
4. **No format without a test.** Each format comes in with a small test file in `tests/data/` and a check in `tests/smoke.sh`: it decodes, the dimensions are right, the color descriptor and the orientation are correct.
5. **Fidelity:** the decoder must deliver native depth and the color metadata (ICC, CICP, attributes). Converting to 8 bits or to sRGB inside the backend is forbidden.
6. **Security (D-38):** fuzzed specialist libraries run in the application; the long tail runs only in the decode worker process, with an allow-list, limits and a timeout; the CI fuzz smoke test corrupts every test file; full fuzzing (libFuzzer, 10,000 files) stays in Phase 5.
7. **Appendix A is the contract.** Each format has a status (v1, to verify, out) and the status only changes with a test.

---

## 7. Fidelity criteria

### 7.1 SDR (F1–F12)

| ID | Criterion | Threshold |
|---|---|---|
| F1 | Decoding | **Lossless:** bit-identical to a reference decoder. **Lossy:** identical to the same library, or PSNR ≥ 50 dB against an independent decoder. |
| F2 | Bit depth | No intermediate quantization below FP16; error ≤ 1 LSB at the output depth. |
| F3 | Color management | Against an independent reference (ArgyllCMS `cctiff`, colour-science): mean ΔE00 ≤ 0.5, p99 ≤ 1.0, maximum ≤ 2.0. Corpus: sRGB, P3, Adobe RGB, ProPhoto, Rec.2020, gray, CMYK, ICC v2 and v4, LUT profiles, CICP. |
| F4 | No profile | Assumes sRGB; the panel shows "assumed". |
| F5 | Display profile or mode | Change of display, of profile or of HDR on/off → reconversion in ≤ 500 ms. |
| F6 | Transparency | Error ≤ 1 LSB against a reference composite; no halos. |
| F7 | Orientation | EXIF 1 to 8 correct in all formats, never applied twice. |
| F8 | Scaling | **100 %:** 1 image pixel = 1 physical pixel, bit-identical at DPR 1, 1.25, 1.5 and 2. **Downscaling:** mipmaps in linear light and trilinear filtering, mean ΔE00 ≤ 1 against a Lanczos reference. **Upscaling ≥ 200 %:** nearest neighbor, exact. |
| F9 | Animation | Delays ±10 ms; correct frame disposal and blending. |
| F10 | Color metadata | Match the file. |
| F11 | Gain map | The base image is shown exactly until Phase 5 applies the gain map. |
| F12 | No silent degradation | Any degradation is visible in the info panel. |

### 7.2 HDR (H1–H6)

| ID | Criterion | Threshold |
|---|---|---|
| H1 | PQ and HLG decoding | PQ in absolute nits (ST 2084), relative error < 0.1 %. HLG with the BT.2100 OOTF for the display peak. |
| H2 | Output mapping | Content ≤ display peak is reproduced unchanged (absolute nits in PQ; relative to reference white in HLG and SDR). Verified by reading back the offscreen target. |
| H3 | SDR white | SDR and UI at the system's `sdrWhiteLevel` (Windows) or 1.0 EDR (macOS). |
| H4 | Tone mapping | BT.2390 EETF applied only above the knee; identity below it (verified by readback). Option "signal without tone mapping + clipping warning". |
| H5 | HDR metadata | MaxCLL, MaxFALL and *mastering display* shown when present. |
| H6 | SDR display | HDR content on an SDR display is mapped by a documented operator (BT.2408/2390) and the interface says so. |

### 7.3 Stated limits

The application guarantees fidelity **up to the buffer handed to the system**. What happens after that is outside its control:
- **Display gamut and peak:** the display's own tone mapping, ABL.
- **macOS EDR headroom:** varies with brightness and ambient light.
- **Windows:** the display's tone mapping in HDR10 and the SDR brightness slider.
- **Linux:** HDR is immature and depends on the compositor and Mesa.
- **Calibration:** accuracy depends on the monitor's calibration and profile.
- **System adjustments:** Night Light and True Tone alter the image.

**It does not replace a reference monitor** connected over SDI. See O4.

---

## 8. User experience

**Principles:**
- The image is the interface; there are no permanent bars.
- Controls appear on mouse movement and disappear 1.5 s later. The system's reduce-motion setting is respected.
- The keyboard is enough for everything.
- There are no modal dialogs in the normal flow (the trash confirmation can be switched off from the dialog itself).

**Floating bottom bar** (appears when the mouse moves):
- previous and next (there are also clickable zones at the edges);
- file name and position in the folder;
- zoom percentage (toggles between fit and 100 %);
- view rotation;
- info;
- fullscreen.

**Info overlay (E14, D-34, Shift+I):** one compact line at the top, its own renderer layer at SDR white, so the viewport never moves. Defaults: in full screen, shown while the pointer is in the top band and hidden after 1.5 s; in a window, hidden. Fields (on by default: the six of ImageGlass #2475): file name, dimensions, file size, zoom, colour space, modification date; optional: position in the folder, output mode. Settings: order and choice of fields, background opacity (0–100 %), text opacity, text outline, hide delay.

**Info panel (I key, D-34)**, the single place for every detail, label and value columns:
- **File:** name, folder, size, modified, position in the folder;
- **Image:** dimensions and megapixels (and the reduced size if the GPU limit applied), format, bit depth, floating point, alpha, EXIF orientation;
- **Color:** source description (CICP, ICC, format attributes or assumed), peak in SDR-white units and nits;
- **Camera** (only when present): make and model, lens, exposure time, aperture, ISO, focal length, date taken;
- **View and output:** zoom, rotation and mirroring, exposure; output mode and display (SDR/HDR, peak, SDR white), graphics backend, tone mapping or clipping;
- HDR metadata (MaxCLL, MaxFALL, mastering display) once the decoders expose them (§14).

**Context menu** (the single source of actions, D-30; as in release 0.2, later items in brackets):
- Open…, Open Recent ▸ (10 files, Clear Menu), [Open With ▸], Show in Explorer/Finder/File Manager
- Copy Image, Copy File Path, Rename…, Move to Trash/Recycle Bin…, Delete Permanently…, Undo Move to Trash
- **View ▸** Zoom In, Zoom Out, Fit to Window, Actual Size (100 %) | Full Screen, Information Panel, Information Overlay, Checkerboard Background
- **Image ▸** Rotate Clockwise, Rotate Counterclockwise | Flip Horizontally, Flip Vertically
- **Color & HDR ▸** Increase/Decrease/Reset Exposure | Tone Mapping (BT.2390), Highlight Altered Pixels
- **Go ▸** Previous, Next | First, Last | Slideshow | Pause Animation, Previous Frame, Next Frame
- Settings…, **Help ▸** About imageViewer, About Qt, Quit

**Shortcuts:**

| Key | Action |
|---|---|
| ← → / PgUp PgDn | Previous / next |
| Home / End | First / last |
| `+` `−` / wheel | Zoom |
| `0` | Fit |
| `1` | 100 % |
| F / F11 / double-click | Fullscreen |
| Esc | Exit fullscreen |
| R / Shift+R | Rotate ↻ / ↺ |
| H / V | Flip horizontally / vertically |
| Ctrl/⌘+Shift+E | Show in folder |
| Ctrl/⌘ + / − / 0 / 1 | Zoom in / out / fit / 100 % (also without Ctrl) |
| Mouse back / forward | Previous / next |
| I | Info panel |
| Shift+I | Info overlay (E14) |
| Del | Trash |
| Shift+Del | Delete permanently (always asks) |
| B | Checkerboard behind transparency |
| Ctrl/⌘+Z | Undo |
| F2 | Rename |
| Ctrl/⌘+C | Copy image |
| Ctrl/⌘+Shift+C | Copy path |
| Ctrl/⌘+O | Open |
| K | Pause animation |
| `,` `.` | Previous / next frame |
| S | Slideshow |
| E / Shift+E / Ctrl+E | Exposure +½ / −½ EV / reset |
| C | Highlight altered pixels (clipped or tone mapped) |
| T | Tone mapping on/off |
| Ctrl/⌘+, | Settings |

**Settings** (D-29; releases 0.1 and 0.2 in bold, later ones plain):
- **General:** **language** (system default or one of 37), **confirm before moving to the trash**, **reopen the last image at startup**;
- **Window:** **background** (black, dark gray `#212121`, gray, light gray, white, custom), **checkerboard behind transparency**, **remember window size and position**; theme;
- **Information:** **show the information panel**; **top overlay: visibility in full screen and in a window (always, on hover, hidden), fields and their order, background opacity, text opacity, text outline, hide delay**;
- **Navigation:** **loop at the ends of the folder**, **side click zones and their width**, **sort by name, date modified or size, ascending or descending**, **preload the next and previous images**, **slideshow interval**; mouse wheel behavior;
- **Color & HDR:** **display output (automatic, SDR, HDR10)**, **tone mapping at startup (BT.2390)**; SDR target profile (automatic or custom ICC);
- "Associations…" button (Phase 4).

### 8.1 Feature parity: qView and ImageGlass (D-28)

Sources: qView 7.1 (2025-07-26) and ImageGlass 9.6.1 (2026-08-05) — documentation, release notes and UI resource/string files; some default values were read from their source files as facts only, no code was copied (D-11) `[doc]`. ImageGlass 10 (beta) is a cross-platform rewrite with HDR tone mapping to SDR (§4.2). Status: ✔ = in imageViewer (0.1 unless another version is given), a version = planned there, — = not planned. Window matching the image, zoom modes and title bar modes moved from 0.2 to 0.3 on 2026-10-07 to keep 0.2 reviewable, then to 0.4 (D-40).

| Feature | qView | ImageGlass | imageViewer |
|---|---|---|---|
| Background color setting | yes (default `#212121`) | yes | ✔ (presets + custom) |
| Copy image (bitmap + file together) | yes | yes (separate commands) | ✔ (16-bit sRGB bitmap + file) |
| Copy file path | no | yes | ✔ |
| Move to trash, optional confirmation ("do not ask again") | yes | yes | ✔ |
| Delete permanently (always asks) | yes | yes | ✔ 0.2 |
| Undo delete (restore from trash) | yes | no | ✔ 0.2 |
| Rename | yes | yes | ✔ 0.2 |
| Show in folder | yes (selects the file on Windows/macOS) | yes | ✔ (selects on Linux too, via FileManager1) |
| Open with / edit with | yes | yes | 0.4 (D-40) |
| Rotate ±90°, flip H/V (view only) | yes | yes (can be saved) | ✔ (view only; saving: Phase 5) |
| Side previous/next buttons on hover | no | yes (50 px discs) | ✔ (D-31) |
| Mouse back/forward buttons | yes | yes | ✔ |
| First/last image, loop | yes | yes | ✔ (loop optional) |
| Settings window, saved preferences | yes | yes | ✔ (4 tabs) |
| Window geometry and state remembered | yes | yes | ✔ (plus reopen last image, optional) |
| Interface languages | 30 | 43 | ✔ 16 (D-27); 37 since 0.2 (D-32) |
| Recent files | yes (10) | no | ✔ 0.2 (shared by every instance since 0.3, D-44) |
| File information dialog | yes | via system dialog | ✔ 0.2 (consolidated information panel, D-34) |
| Preloading neighbours | yes (±1 / ±4) | yes (0–10) | ✔ 0.2 (±1, D-33; Phase 3 target: ≤ 50 ms) |
| Sort modes | 6 | 9 | ✔ 0.2 (name, date modified, size; ascending or descending) |
| Slideshow | yes | yes | ✔ 0.3 (E11, D-40) |
| Animation controls | yes | yes | ✔ 0.3 (E6, D-40: pause, frame stepping) |
| Shortcut editor | yes | config file only | 0.4 (D-40) |
| Window matches image size | yes | yes (Window Fit) | 0.4 (D-40) |
| Titlebar text modes | yes (4) | yes (tags) | 0.4 (D-40) |
| Fullscreen information overlay (E14) | no | requested in #2475 | ✔ 0.2 (D-34) |
| Zoom modes (fit width/height, fill, lock) | partly | yes (6 + lock) | 0.4 (D-40) |
| Checkerboard behind transparency | no | yes | ✔ 0.2 |
| Thumbnail gallery, toolbar | no | yes | — (D-08 minimalism; reconsider after 1.0) |
| Color picker / pixel value | no | yes | Phase 1 (E9, code value and nits) |
| Channel view, invert colors | no | yes | after 0.4 |
| Crop, resize, save edited | no | yes | — (viewer, not editor) |
| Open URL, paste image | URL | paste | after 0.4 |
| Real-time folder watching | no | yes | ✔ 0.2 (E2: folder and current file) |
| Frameless window, always on top | no | yes | after 0.4 |
| Print, share, set as wallpaper | no | yes | — |
| Color management | display profile, sRGB, P3 | monitor or any ICC | ✔ ICC/CICP into scRGB, HDR output (D-10, D-15); display ICC: Phase 1 |
| HDR output | no | no (v10: tone mapping to SDR) | ✔ scRGB/EDR/HDR10 |

---

## 9. Phases

Relative effort in parentheses. Total estimate to v1: 16 to 24 weeks for a senior engineer with AI assistance `[estimate; low confidence]`.

### Release 0.1 (October 2026)
- [x] English-only repository: code, UI strings, plan, README, CLAUDE.md (D-27)
- [x] Interface in 16 languages: Qt Linguist, language setting, right-to-left layout, `QLocale` numbers, consistency gate (D-27); machine translations pending native review (D-P13)
- [x] Command table and grouped context menu (D-30); copy image and path, move to trash with optional confirmation, show in folder, flip horizontal/vertical, rotate both ways, first/last, About (D-28)
- [x] Clickable side zones and mouse back/forward buttons (D-31)
- [x] Settings window (General, Window, Navigation, Color & HDR) and saved session (D-29); display output chosen in Settings
- [x] Interaction test on Xvfb with xdotool: side zones, flips/rotations, trash with confirmation, saved session (`tests/ui_test.py`) `[test]`
- [x] Public README and `CONTRIBUTING.md` (DCO, translations)
- [x] Green CI (runs 21–23), PR #2, release `v0.1` published by CI (run 28) `[test]`

### Release 0.2 (published 2026-10-07)
- [x] 37 interface languages (D-32): Korean, Italian, Turkish and the 18 missing EU languages; all 220 strings translated in the 36 languages (machine translation, native review pending, D-P13); `check_translations --require-complete` green; the CI also fails when the `.ts` files are out of date `[test]`
- [x] Preloading of the next and previous images: RAM cache with a budget, shared pixels, direction of travel first, setting (D-33)
- [x] Consolidated information panel: file, image, colour, camera EXIF, view and output (D-34); `--info` prints the camera line
- [x] Top information overlay (E14, D-34): fields and order, visibility in full screen and in a window, opacities, outline, hide delay, Shift+I; new Settings tab "Information"
- [x] File actions (D-28): rename (F2), delete permanently (Shift+Delete, always asks), undo move to trash (Ctrl+Z), Open Recent (10, Clear Menu)
- [x] Folder: sort by name, date modified or size, ascending or descending; watch the folder and the current file (re-list; reload a changed image keeping the view)
- [x] Checkerboard behind transparency (Settings, View menu, key B), anchored to the image
- [x] Tests: smoke (camera EXIF), interaction test (undo, preload cache, rename, permanent delete, files added, deleted and rewritten by another program, top overlay leaves the image untouched, checkerboard, restart with 0.1 settings) `[test]`
- [x] Full adversarial review of the whole codebase (D-35), 2026-10-07: three parallel reviewers (loading, cache and decoding; GPU, CI, tests and docs; commands, settings and interface), every finding reproduced or traced before fixing. Confirmed and fixed, among others: settings of the "general" group never loaded on Linux since 0.1 (INI `[%General]`; group renamed with migration, restart test); an endless preload loop when neighbours do not fit the memory budget (49 decodes in 10 s → 2); an image changed during its decode stayed stale; Ctrl+Z could restore another file that took the same trash name; a decode result could be dropped (QFutureWatcher); AZERTY digits; a double-click at a folder end toggled full screen; file names rendered as HTML in dialogs; bidi spoofing of file names; RTL reordering of paths; i18n rule gaps. Left on purpose: §14 ("Known limits left after the 0.2 review")
- [x] Green CI, PR #3, release `v0.2` published by CI (2026-10-07)

Moved to 0.3: window matching the image size, zoom modes, title bar modes, slideshow, shortcut editor (§8.1).

### Release 0.3 (published 2026-10-08, D-40: formats first)
- [x] Quitting from the macOS application menu no longer crashes (the window's own signals reached a half-destroyed `ViewerWindow`; reproduced in CI with lldb and on Linux with gdb); OpenImageIO shut down before exit; quit test on macOS in CI `[test: runs 49–51]`
- [x] Side zones 100 px by default and Apply in the Settings (D-36); `--formats` prints what each decoder of the build reads
- [x] Format registry and detection by content (D-38): 32 formats at first, 50 after the decode worker and the review; `--formats` table; CI decodes every format's test file on the 3 systems (camera RAW has none: LibRaw refused the synthetic DNG files we could write, §14); CI configures with `IMAGEVIEWER_REQUIRE_ALL_DECODERS`
- [x] JPEG XL through libjxl: native depth (16-bit sRGB exact), enumerated colour encoding or ICC, HDR (BT.2020 PQ at 1000 nits), EXIF camera data `[test: smoke]`; WebP through libwebp now honours its ICC profile (Display P3 red 1.225 instead of 1.0) `[test: smoke]`
- [x] Animation: GIF (OpenImageIO subimages), WebP (libwebp), APNG (frames rebuilt as PNGs, composited with the APNG dispose and blend operations), JPEG XL (libjxl); every frame converted like the first, kept in memory up to an eighth of the RAM; loop counts; frame times as browsers apply them; the next frame decoded on its own thread; nothing decoded while the window is hidden; K pauses, `,` `.` step frames (E6) `[test: smoke, animation_test.py on Vulkan and OpenGL]`
- [x] Slideshow (E11): S starts and stops it, Esc stops it, a manual step restarts the interval, the end of a folder that does not loop ends it; interval in the Settings (1–3600 s, default 5) `[test: animation_test.py]`
- [x] AVIF image sequences through libheif's track API (1.23, built in when present; CI requires it): native depth (the first frame decoded as coded to learn it, since 16-bit RGB output turns 8-bit files into 10-bit samples `[test: local probe]`), colour from the frame's nclx or ICC, else the still image's, with libheif's nclx passthrough; edit list ignored and loops ours; a file without an edit list loops forever, as in browsers `[test: smoke with libheif 1.23.5 built locally]`. A one-frame animation (APNG, JPEG XL, AVIF) is shown as a still
- [x] Fixed: narrow-range AVIF expanded twice in the CI builds: OpenImageIO 3 passes the nclx range flag on as CICP, but libheif has already returned full-range RGB (`[code: OIIO 3.1.14 heifinput.cpp]`, `[test: libheif 1.23.5 decodes the narrow file as RGB 255/129/0]`); the flag is ignored for HEIF/AVIF, smoke checks still and sequence
- [x] HEIC through the system (D-39): ImageIO on macOS, drawn by ColorSync into extended linear sRGB floats (the working space, so no second conversion); WIC on Windows at 8 bits per channel with the embedded ICC profile (WIC treats its 16-bit integer formats as linear and would change the curve `[doc: .NET PixelFormats.Rgba64, "gamma of 1.0"]`); elsewhere OpenImageIO, which decodes HEIC when the distribution's libheif has an HEVC plugin (libheif-plugin-libde265) `[test: local, Ubuntu 24.04]`; otherwise a note on how to get a decoder (the Microsoft Store extensions on Windows). `--formats` reports `system-missing`; HEIC files are listed in folders either way `[test: smoke, both Linux paths]`. ImageIO decodes the test file on the macOS runner within 0.02 `[test: runs 59-62]`. Not yet verified: WIC with the extensions installed (CI runners have none) and 10-bit or HDR HEIC, which this path reads as SDR
- [x] Decode worker process with GraphicsMagick 1.3.48 (D-38), `worker.cpp`: the same executable started as `--decode-worker <CODER> <max pixels>`, the file's bytes on standard input, the pixels (native 8 or 16 bits, straight alpha, ICC profile, orientation) on standard output. 16 coders (XCF, PCX, DCX, PICT, WPG, MIFF, Sun raster, VIFF, DICOM, VICAR, MATLAB, TIM, CUT, MacPaint, Alias PIX, OTB); every other coder is unregistered before anything is read (183 → 16; PS, EPS, PDF, MSL, MVG, TXT, URL gone, a PostScript blob is refused, so Ghostscript cannot start `[test: local probe]`); the coder is fixed by the registry (`CODER:` prefix, no guessing from content); limits: pixels as asked, 16 bytes per pixel of memory up to 8 GiB, no disk or mapped cache, 4 GiB read, one thread; killed after 30 s; an empty temporary working directory; `MAGICK_*` variables removed; no core dump or Windows error dialog. The viewer checks the answer's header before accepting data and reads no more than it announces. Overlay port without any delegate library (`packaging/vcpkg/ports/graphicsmagick`, SourceForge tarball, SHA-512 pinned; no `gm` tool or `delegates.mgk` installed). GraphicsMagick 1.3.48 does not composite XCF layers (its code for it is disabled): the worker composites them with their blend modes, offsets and visibility `[test: smoke, 50 % blue over orange]`; limitations recorded: XCF per-pixel alpha is reduced to on/off by GraphicsMagick's tile reader `[code: coders/xcf.c]`, CUT palettes (`.pal` files) are not read. Test files: 10 written by GraphicsMagick, 6 built from their specifications by `tests/longtail_data.py` `[test: smoke]`
- [x] Fuzz smoke test (`tests/fuzz_smoke.py`, D-38 point 5): every test file corrupted 12 ways (truncations, bit flips, random bytes, header fields, all-ones fields, repeated blocks; seeded per file) must decode or fail cleanly within 60 s, on the 3 systems in CI; a crash, hang or malformed answer of the decode worker counts as a failure too `[test: 468 cases locally, all clean; 3,120 cases with 80 mutations per file; after the review 780 cases, all clean with D-45]`
- [x] Full adversarial review of the whole codebase (D-35), 2026-10-08: five parallel reviewers (structure and documents; colour and output; interface and playback; decoders and formats; the decode worker), every finding reproduced (a script, a crafted file or the old binary) before its fix. Confirmed and fixed: the decoding and output defects of D-42 (transparent GIF, straight alpha divided twice for BMP, DDS, ICO, JPEG 2000 and SGI, GIF loop counts, the clip of wide-gamut colours on wide outputs, the peak statistic, HDR animations tone-mapped with the first frame's peak, lossy JPEG XL clipped); DDS bits per sample; a TGA detected as a Windows cursor; out of memory while decoding a frame ended the application; an AVIF sequence whose frames differ from the declared size, and libheif's own pixel limit; a GIF animation kept its file open (now read from memory); the worker issues of D-43; an animation still playing after its image was removed; a finished animation that would not play again; the slideshow advancing behind a dialog or a menu, ignoring a new interval, and disabled while running; the frames of animations no longer on screen kept outside the cache budget (four 1024×1024 GIFs: 950 → 640 MiB); a second instance overwriting preferences and recent files (D-44); Undo Trash refused for a trash at the top of another volume, whose record holds a relative path (freedesktop specification); then, from CI and the fuzz smoke test: OpenImageIO's Softimage PIC reader crashing on a truncated file and its 2.4 IFF reader overflowing (D-45), and no GraphicsMagick coder registered on Windows, where `studio.h` enables module loading for every MSVC `/MD` build (fixed in the overlay port; the worker's self-check now names its reason and refuses when an allowed coder is missing). Structure: `apng.cpp` split from `codecs.cpp`; translator comments attached to the right strings; numbers in output descriptions and memory messages through `QLocale`; the quit test fails when a sent quit leaves the viewer running; documents brought up to date. New tests: pixel checks for 20 more formats, straight alpha, GIF loops, a wide-gamut render case, the EXIF of a JPEG XL. Left on purpose: §13 (D-P15) and §14 ("Known limits left after the 0.3 review")
- [x] Green CI on the review fixes: run 62 (`a8a1e66`) on the 3 systems, packages verified on clean machines `[test]`
- [x] PR #4 merged and release `v0.3` published (2026-10-08); quitting from the macOS menu confirmed by Nuno on a MacBook Pro (M5 Max) `[test: manual]`

Moved to 0.4 (D-40): window matching the image size, zoom modes, title bar modes, shortcut editor, Open With.

### Release 0.4 (planned)
Corrections from Nuno's review of 0.3 (2026-10-08):
- [x] Flips named as mirroring: "Espelhar horizontalmente/verticalmente" in Portuguese; the mirror verb also in Romanian, Swedish, Irish, Maltese and Indonesian, where it is the usual term (German, Dutch, Russian, Polish, Italian, Danish, Finnish and others already used it; English keeps "Flip")
- [x] Information panels legible over any image and alike (D-49): one style for both, blended as in SDR in every output, labels ≥ 4.5:1 checked by `--panel-check` in `render_test.py`
- [x] Slideshow moved from the Go menu to the View menu
- [x] Keyboard convention (D-48): first and last image Shift+← and Shift+→ (Home and End kept), Rename also Return, Delete Permanently also Cmd+Shift+Backspace; `ui_test.py` uses Shift+→
- [~] Dependency audit (D-46), 2026-10-09 (§5): OpenImageIO 3.2.1.1, GraphicsMagick 1.3.49, libheif 1.23.6, expat 2.9.0 and minizip-ng 4.2.2 as overlays, vcpkg baseline 0699a19d, Qt 6.12.0 (D-50). *Missing:* green CI on the new dependencies

From D-40 and §14:
- [ ] Window matching the image size
- [ ] Zoom modes: fit width, fit height, fill, lock zoom
- [ ] Title bar text modes
- [ ] Shortcut editor
- [ ] Open With and edit with
- [ ] Folder listing off the GUI thread
- [ ] Full review (D-35), green CI, PR

### Phase 0 — Foundation (M)
- [x] `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `CLAUDE.md`, `scripts/build-qt-linux.sh`
- [x] QRhi window: chooses the swapchain (SDR, scRGB or HDR10) per system and logs `hdrInfo` *(validated on Linux/Xvfb; Windows, macOS and real HDR still missing)*
- [x] Minimal decoding (OIIO + Qt fallback) → linear scRGB → texture → shader with output modes
- [x] Fit, 100 %, zoom at cursor, pan, previous/next, drag and drop, `QFileOpenEvent` *(still to be validated at runtime)*
- [x] GitHub Actions CI: Windows x64, macOS arm64, Linux x64 → build, tests, `.zip`/`.dmg`/`.tar.gz` packages, package testing on clean machines and automatic release (D-26). Green in run 14 `[test]`. Release `v0.1-alpha` published by the `release` job (run 19) `[test]`. *Installers and AppImage are left for Phase 4.*
- [~] Measurement of package size ✔ (v0.1: Windows 42.5 MB, macOS 37.5 MB, Linux 49.0 MB); startup time to the 1st frame still to measure

**Acceptance:**
- CI green on all 3 systems.
- The artifact opens JPEG, 16-bit PNG, EXR and PQ AVIF.
- Startup to the 1st frame ≤ 300 ms (warm) on the reference hardware.
- On an HDR display (MacBook XDR and Windows HDR), the HDR swapchain is chosen and 1000-nit PQ shows highlights above SDR white (manual check).

### Phase 1 — Color and HDR core (L)
- [~] ICC → scRGB (LittleCMS and direct matrix/curve path, D-23) ✔; analytic CICP (sRGB, BT.1886, gamma, PQ, HLG with OOTF, linear, narrow range) with priority over ICC (D-22) ✔; EXR `chromaticities` ✔; straight alpha and premultiplication in linear light (D-21) ✔. *Missing:* tests with real PQ/HLG files.
- [ ] SdrIcc mode with a 3D LUT from the display profile; Advanced Color/ACM detection on Windows; SDR white
- [~] BT.2390 EETF (D-15) ✔; HDR→SDR mapping ✔; exposure ✔; altered-pixel warning (clipped or tone-mapped) ✔; PQ in absolute nits ✔; indication in the interface (H6) ✔. *Missing:* pixel value readout (code value and nits); test with real PQ/HLG files (the current corpus is linear).
- [~] Fidelity harness (D-16): offscreen render and readback ✔ (Vulkan and OpenGL on Linux); synthetic linear corpus ✔. Independent hue check (R:G:B ratios) ✔; the test confirms the backend used ✔. *Missing:* PQ/HLG corpus; the harness also runs in CI on Windows (D3D11/WARP) and macOS (Metal) ✔.

**Acceptance:** F1–F7, F10, F12 and H1–H6 in CI.

### Phase 2 — Format coverage (L)
- [~] Qt for CUR, ICNS, XBM, XPM, WBMP and SVG ✔, own APNG reader ✔ (0.3). *Missing:* FFmpeg back end (image2), lunasvg
- [~] Animation (F9) ✔ (0.3). *Missing:* multipage and multi-part, tiling for huge images, RAW options
- [~] Registry with one test file per format, decoded in CI on the 3 systems ✔ (0.3, D-38; D-P05 resolved: GraphicsMagick in the decode worker). *Missing:* the Appendix A entries outside the registry, a camera RAW test file

**Acceptance:** 100 % of the v1 entries in Appendix A open and pass F1/F7; no crash on the corpus.

### Phase 3 — Viewer and UX (L)
- [x] Context menu (D-30), settings (D-29), i18n (D-27), file actions: copy image/path, trash, show in folder (D-28)
- [x] Cache and preloading, *file watcher*, info panel (E8), rename/undo trash/recent files (§8.1): release 0.2 (§9). macOS menu bar, themes, accessibility review: later.
- [x] Animation controls (E6) and slideshow (E11): release 0.3.
- [x] **E14 / ImageGlass #2475 — configurable info overlay** (release 0.2, D-34):
  - [x] Overlay at the top in fullscreen, without reserving space; zoom, pan and scale unchanged when showing or hiding it.
  - [x] Visibility modes: always visible · hidden · show on mouse hover and auto-hide (activation zone at the top; configurable delay).
  - [x] Appearance: background opacity (0–100 %, including transparent), independent text opacity, text outline.
  - [x] Configurable and reorderable fields: name+extension, native dimensions, file size, zoom %, profile or color space, modification date (+ SDR/HDR output mode, position in the folder).
  - [x] Updates on change of image, zoom or output mode; persistent settings (`QSettings`).
  - [~] Automated test: the image is identical, pixel for pixel, with the overlay visible and hidden (Xvfb capture, `ui_test.py`) ✔; the overlay text at SDR white level is checked by construction (SDR overlay texture, §6.2), not by a test.

**Acceptance:**
- F8.
- Next image, already preloaded, shown in ≤ 50 ms (p95).
- 24 MP JPEG sharp in ≤ 300 ms.
- Stable RAM after 1,000 navigations.
- Test with 5 to 8 users on 8 tasks: ≥ 90 % success.

### Phase 4 — Integration and distribution (M)
- [ ] Associations:
  - Windows: ProgID, `OpenWithProgids`, Capabilities, `ms-settings:defaultapps`;
  - macOS: UTIs and `LSHandlerRank` Alternate;
  - Linux: `.desktop`, MIME, AppStream.
- [ ] Single instance; display, profile and HDR change events (F5)
- [ ] Installers: Inno Setup; DMG with codesign and notarization; AppImage and Flatpak. Signing conditional on CI secrets.
- [ ] End-to-end tests of the associations on all 3 systems
- [x] `LICENSE` (Apache-2.0) and `NOTICE` inside the packages (D-18)
- [x] Third-party licenses in the packages (`third-party/`): vcpkg `copyright` files, `Qt.txt` with the origin of the source code, LGPL-3.0 and GPL-3.0 texts; license gate in CI (D-25) and a check of the number of files in the package. Verified on all 3 systems (run 14) `[test]`.
- [~] Public product README (D-27: English) with supported formats, fidelity criteria and downloads ✔; screenshots still to add. `CONTRIBUTING.md` with DCO ✔.

**Acceptance:**
- Double-click, `open`, `start` and `xdg-open` open the right file.
- A second file reuses the instance.
- Uninstalling removes the associations.

### Phase 5 — Professional features and hardening (M)
- [ ] OCIO (ACES, camera logs) for EXR/DPX/Cineon; gain maps (ISO 21496-1, UltraHDR, Apple)
- [~] Fuzz smoke test in CI ✔ (0.3, D-38). *Missing:* coverage-guided decoder fuzzing (libFuzzer), profiling, beta with 10 to 20 users, release candidate

**Acceptance:** 0 crashes on ≥ 10,000 corrupted files; F and H green on all 3 systems.

---

## 10. Tests

**Synthetic corpus** (generated by the project):
- 8-bit, 16-bit and float ramps;
- patches in various gamuts;
- alpha gradients;
- zone plate pattern;
- 8 orientations;
- PQ and HLG ramps from 0 to 10,000 nits.

**Public corpus** (licenses to be confirmed):
- PngSuite;
- exif-orientation-examples;
- libjxl conformance;
- libavif and libheif tests;
- ICC v4 images (color.org);
- raw.pixls.us;
- OpenEXR test images.

**Independent references:**
- ArgyllCMS (its own color engine);
- colour-science;
- ST 2084 and BT.2100 analytic formulas.

The product's own engine is never used as the reference.

**HDR in CI:** `tests/render_test.py` (D-16). It generates a synthetic linear corpus (PFM, 512 levels from 0.2 to 6090 nits in 4 hues, one of them outside BT.709). It draws it with `imageViewer --render` on the SDR, EDR, scRGB and PQ outputs, with and without tone mapping and with exposure. It reads the RGBA32F target back from the GPU and checks:
- GPU = CPU reference (relative error ≤ 1e-3; measured: ≤ 8e-5) `[test]`;
- identity up to the BT.2390 knee computed in Python with the ITU formula, against the input rounded to FP16 as the decoder stores it (error ≤ 2e-4; measured ≤ 7.3e-5, on the PQ output) `[test]`;
- never above the peak; monotonicity; the content peak lands on the output peak; plain clipping without tone mapping `[test]`.

On the PQ outputs, neutrality (R = G = B for a gray) is checked on the BT.2020 signal itself, with a tolerance of 5e-5, 1/20 of a 10-bit step. In nits, the PQ curve amplifies normal GPU imprecision. It also checks that tone mapping preserves the input's R:G:B ratios (D-15; error ≤ 1e-3, measured 3.4e-4 in the PQ case) and that the backend used is the one requested (a silent fallback from Vulkan to OpenGL made the test pass without testing Vulkan).

It passes on Vulkan (lavapipe) and OpenGL (llvmpipe) `[test]`. It runs in the Linux CI; on Windows (D3D11) and macOS (Metal) it runs with the `offscreen` platform. On macOS, exit code 4 (no GPU) is only a warning.

**Smoke tests** (`tests/smoke.sh`, `--info` without a graphics platform): P3 ICC, EXR with premultiplied alpha, PNG, BMP and SGI with straight alpha, a transparent GIF, EXIF orientation, EXIF camera data with control characters removed (`camera.jpg`, and `camera.jxl` with a compressed EXIF box), PFM, 16-bit and PQ JPEG XL, every frame and duration of the test animations and their loop counts, narrow-range AVIF, HEIC through the system or the note on getting a decoder, the exact pixels of the long-tail test files (orange (255, 128, 0) → linear (1, 0.2158, 0)), and the test file of every format in the registry (strict in CI, where `$CI` is set). The test files no common tool writes correctly are built from their specifications by `tests/longtail_data.py`, whose docstring also lists how the others were written. They run in the build and again on the package on a clean machine (`verify` job).

**Fuzz smoke test** (`tests/fuzz_smoke.py`, D-38): every test file corrupted 12 ways must make `--info` exit 0 or 1 within 60 s; a crash, an abort, a hang, or the viewer's log line for a crashed, hung or malformed decode worker fails it. Mutations are seeded per file, so a failure reproduces from its name. On the 3 systems in CI.

**Animation test** (`tests/animation_test.py`, Xvfb + xdotool, Vulkan and OpenGL): an animated GIF plays its frames in turn, K pauses, `.` and `,` step, K plays again; S starts the slideshow, which moves to the next image and back, and Esc stops it.

**Quit test** (`tests/quit_test.py`, macOS runner only): quitting through the application menu, Cmd+Q or a quit Apple event, right after the image appears and when idle, must end the process cleanly without a crash report; a failing round runs again under lldb for the backtraces.

**Interaction test** (`tests/ui_test.py`, Xvfb + xdotool, Vulkan and OpenGL): three noise images in a folder; hovering the right edge shows the button, clicks on the sides navigate, H/V/R/Shift+R flip and rotate (compared pixel for pixel with numpy's flips and rotations), Delete asks first and Return moves the file to the trash (checked in `$XDG_DATA_HOME/Trash`); since 0.2 also: Ctrl+Z restores it with no trash record left, the step to the neighbour comes from the preload cache (log), F2 renames, Shift+Delete asks with Cancel as default and then deletes, a file added, deleted or rewritten by another program appears, is replaced or is shown again, Shift+I shows the top overlay without moving the image by a pixel, and B shows the checkerboard behind a transparent image (cells compared with the expected pattern); Ctrl+Q saves the session (checked in the settings file). The application runs with its own `HOME`, `XDG_CONFIG_HOME` and `XDG_DATA_HOME` `[test]`.

**Translations gate** (`tests/check_translations.py`): every finished translation keeps the source's placeholders, `&&`, `;;`, `(*)` and HTML tags; on release tags every language must be complete (D-27).

**Manual HDR:** MacBook Pro XDR, HDR10 display on Windows 11, KDE Plasma 6 HDR (best effort). Colorimeter with ArgyllCMS `spotread`.

---

## 11. Distribution and signing

| OS | Build | Package | Signing |
|---|---|---|---|
| Windows x64 | MSVC, vcpkg `x64-windows`, Qt via aqt | Inno Setup + portable zip | Azure Trusted Signing or an OV certificate via `signtool` (optional). Unsigned → SmartScreen warning. |
| macOS arm64 | Clang, `arm64-osx-dynamic` | `.app` + DMG | `codesign --options runtime` + `notarytool` + `stapler`. Requires the Apple Developer Program (≈99 USD/year) `[knowledge]`. Without notarization, Gatekeeper blocks the app and the user has to allow it in System Settings. |
| Linux x64 | GCC, `x64-linux-dynamic`, built on the oldest supported glibc baseline (today: Ubuntu 24.04) | AppImage (linuxdeploy-plugin-qt); Flatpak (KDE runtime) | Not needed; optional GPG |

Qt is deployed with `qt_generate_deploy_app_script`. The vcpkg DLLs and dylibs are copied by the deploy step.

The first dependency build takes 30 to 90 minutes per system `[estimate]`; after that it is kept in the vcpkg binary cache (`files` provider + `actions/cache`).

**Before every pull request (D-35):** at the end of each release's development and before opening its PR, a full adversarial review of the whole codebase, not only the diff.
1. **Scope:** `src/` (including the shaders), `CMakeLists.txt` and presets, `tests/`, `.github/workflows/`, `packaging/`, `scripts/`, `translations/` and the documents that make claims about the code (README, this plan, CLAUDE.md).
2. **Reviewers:** several independent reviewers in parallel, one per area, each looking for the four kinds of defect:
   - **bugs:** logic and edge cases (empty folder, one image, huge, corrupt, read-only or vanished files, right-to-left languages, HiDPI, GPU device loss), threads and object lifetimes, error paths;
   - **security:** every file is untrusted input (dimensions, allocation sizes, integer overflow, decoder limits); paths and process arguments (never through a shell); clipboard, drag and drop and stored settings validated; no secrets in the repository; licences (the CI gate);
   - **performance:** nothing slow on the UI thread (decoding, file I/O, folder listing), memory peaks and the cache budget, copies of pixel buffers, GPU uploads, work done per frame;
   - **structure:** the rules in CLAUDE.md (English, `tr()`, commands, settings, file-size limit of D-08), duplicated logic, dead code, names, comments that no longer match the code.
3. **Verification:** each finding is reproduced (a test, a script or the exact code path) before it is fixed; findings that do not reproduce are discarded with a one-line reason.
4. **Fixes:** every confirmed finding in the release's scope is fixed; anything deliberately left goes to §13 or §14 with its reason. No PR is opened while a confirmed bug or security finding is open.
5. **Re-test:** a build without warnings, `tests/smoke.sh`, `fuzz_smoke.py`, `render_test.py`, `screen_test.py`, `ui_test.py` and `animation_test.py` on Vulkan and OpenGL, `check_translations.py --require-complete`, and CI green on the 3 systems (including `quit_test.py` on macOS).
6. **Dependencies (D-46):** every dependency checked against upstream's latest stable release; the version table and any recorded exception go in the PR.
7. **Record:** the PR description lists the areas reviewed, the confirmed findings with their fixes, and what was deferred.

**Deleting old CI artifacts** (for example those built before D-25, which contain x265): artifacts belong to workflow runs, not to pull requests; GitHub does not delete pull requests (they can only be closed), and closing one leaves its runs and artifacts in place. In the repository: *Actions* → workflow **build** → open the run → at the bottom of its summary page, under **Artifacts**, the trash icon next to each artifact (it cannot be undone) `[doc: github/docs, remove-workflow-artifacts.md]`. Alternatively the run's **⋯** menu → *Delete workflow run* → confirm, which deletes the whole run `[doc: github/docs, delete-a-workflow-run.md]`, including its logs and artifacts `[knowledge]`. Artifacts expire on their own after the retention period (90 days unless the repository sets another) `[doc]`. The runs with x265 are #2 to #9 (commits `8d51f14` to `3020de6`); runs #1, #10 and #11 have no such artifacts `[test: workflow run listing, 2026-10-07]`.

**Publishing a release (D-19, D-26):**
1. On GitHub: *Releases* → *Draft a new release* → *Choose a tag* → type `vX.Y` or `vX.Y-suffix` (for example `v0.2` or `v0.3-beta`) → *Create new tag on publish*, target `main` → tick *Set as a pre-release* if it has a suffix → **Publish release**. Alternatively: `git tag v0.2 && git push origin v0.2`.
2. The `release: published` event (or the tag push) runs the workflow **as it exists in the tagged commit**: it builds on the 3 systems, tests, packages (`.zip`, `.dmg`, `.tar.gz`), re-tests each package on a clean machine and confirms that `--version` shows the tag's version.
3. The `release` job attaches the 3 packages and `SHA256SUMS` to the release (creating it if only the tag exists; suffix = pre-release) and generates the provenance attestation if the repository is public.
4. A tag outside the `vX.Y[-suffix]` format does not trigger the workflow on push; if it is published as a release, CMake rejects the version right at Configure and nothing is attached. If the build or the verification fails, the release is left without packages: fix it on `main`, delete the release and the tag, and publish again.

Note: a release whose tag points to a commit without this workflow (like the `v0.1-alpha` created at `74ddc8b`) never receives packages; it has to be recreated on a commit that has it.

The Inno Setup installer, AppImage and signing arrive in Phase 4.

**For signing** (Phase 4), the GitHub secrets are:
- **macOS:** `APPLE_CERTIFICATE_P12`, `APPLE_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD`;
- **Windows:** Azure Trusted Signing credentials or those of an OV certificate.

**Local development on Windows** (optional; not needed for publishing):
- Visual Studio 2022 or 2026 (or the Build Tools) with the "Desktop development with C++" workload (MSVC, Windows SDK, CMake and Ninja included).
- Git.
- vcpkg: `git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg`, then `bootstrap-vcpkg.bat`, then `setx VCPKG_ROOT C:\dev\vcpkg`.
- Qt 6.12.0 through the **Qt Online Installer** (the Qt Creator installer only ships the IDE). Components: *MSVC 2022 64-bit*, *Qt Shader Tools* and *Qt Image Formats*; Qt Creator is optional as an IDE.
- Commands, in an "x64 Native Tools Command Prompt":
  - `cmake --preset windows -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64`
  - `cmake --build --preset windows`

  The 1st dependency build takes about 1 h.
- Each machine only builds for its own system: a Mac for macOS, Linux (or WSL2) for Linux.

---

## 12. Risks

| Risk | Mitigation |
|---|---|
| R1. Immature HDR on Linux (depends on the compositor and Mesa) | Best effort with SDR+ICC fallback. Test KDE Plasma 6. |
| R2. HDR differences between systems (variable EDR headroom; Windows SDR slider; display tone mapping) | Read `hdrInfo` on every frame or event; show the mode in the panel; stated limits (§7.3). |
| R3. Broken or slow vcpkg builds (FFmpeg, OIIO) on some system | Pin the `builtin-baseline`; *overlay ports*; binary cache; update quarterly. |
| R4. HEVC patents (libde265) | Resolved by D-39: HEIC through the system's licensed decoders (ImageIO, WIC); no HEVC decoder in our packages. |
| R5. Parser attack surface | Up-to-date dependencies fuzzed upstream, no network features; the long tail only in the sandboxed decode worker (D-38, D-43); fuzz smoke test in CI; coverage-guided fuzzing in Phase 5. |
| R6. LGPL non-compliance | Dynamic linking; license notices in the installer; exclusion list (§5). |
| R7. Images larger than the texture limit or than memory | Tiles and pyramid; configurable memory limit. |
| R8. Scope creep | Scope contract (§3). |

---

## 13. Pending decisions

None of them blocks Phase 0. The stated proposal is the one adopted by default.

| ID | Question | Proposal |
|---|---|---|
| D-P01 | ~~**License**~~ **Resolved by D-18.** (calls D-01 into question). Nuno leans toward open source. | **Recommendation (2026-10-06): open source under Apache-2.0** (the license of ASWF tools such as OIIO, OCIO and OpenRV, and with a patent grant), plus: the "Cristallumnis" registered trademark outside the license, CLA or DCO for contributions, and the Cristallumnis AI modules as separate proprietary plugins (*open core*). The dependency rules (§5) still apply. **Nuno's decision.** |
| D-P02 | Platforms and architectures | Windows x64 · macOS arm64 (Intel only if there is demand) · Linux x64. Windows arm64 later. |
| D-P03 | ~~HEIC (HEVC patents)~~ | **Resolved by D-39:** system decoders (ImageIO, WIC), with a note on installing HEVC support. |
| D-P04 | ~~Default tone mapping~~ | **Resolved by D-15.** |
| D-P05 | ~~GraphicsMagick as last resort for legacy formats~~ | **Resolved by D-38:** GraphicsMagick 1.3.48 in the decode worker. |
| D-P06 | Video | Out; only short animations via FFmpeg (gifv, mjpeg). MOV/MXF frames go under O5. |
| D-P07 | Minimum versions | Windows 10 22H2 (best effort) and 11; macOS 14.4 (D-50, Qt 6.12's minimum); glibc of the Linux build baseline. |
| D-P08 | Signing accounts | Apple Developer ID and Azure Trusted Signing (or an OV certificate). |
| D-P09 | Single instance by default; nearest neighbor from 200 % up; relative colorimetric intent with BPC; no automatic updates; no telemetry | Adopted. |
| D-P10 | EETF source peak: the image's absolute maximum (current) or a high percentile (99.9 %) / MaxCLL. | With the absolute maximum, a single specular pixel at 10,000 nits lowers the knee for the whole image (e.g. HDR 7300 nits → SDR: knee at 28 nits). A percentile preserves the midtones and clips the extremes. Decide with real HDR images in Phase 1. |
| D-P11 | ~~Language of the public README and `CONTRIBUTING`~~ | **Resolved by D-27:** English, like everything in the repository. |
| D-P12 | ~~Language list: by total speakers (D-27) or by Cristallumnis' markets?~~ | **Resolved by D-32:** both; Korean, Italian and Turkish added without removing any language, plus every official EU language. |
| D-P13 | Native review of the 36 machine translations | Invite native-speaking contributors (CONTRIBUTING explains how, with Qt Linguist); Nuno reviews European Portuguese; a reviewed language drops the "machine translation" note for that language. Irish and Maltese first (D-32). |
| D-P15 | HLG system gamma: the OOTF of the nominal 1000-nit display (γ = 1.2, current) or the one BT.2100 gives for the actual output peak (γ = 1.2 + 0.42·log10(Lw/1000)) | Keep 1000 nits until the Phase 1 HLG corpus: with the actual peak, the same file would look different on every display, which suits a viewer less than a reference rendering `[inference]`; decide with real HLG images and an HDR display. Found by the 0.3 review (D-35). |
| D-P14 | Linux backend if the Vulkan widget issue (§14) reproduces on real desktops | Use OpenGL when no HDR swapchain is available (Vulkan is only needed for HDR on Linux), keeping Vulkan for HDR. Decide after testing on a Linux desktop with a real Vulkan driver. |

---

## 14. To verify

- Actual behavior of HDR output on hardware: Windows HDR10/scRGB, macOS XDR, KDE Plasma 6 Wayland.
- Interaction with Windows 11 ACM.
- Drag and drop on a plain `QWindow` (no Widgets) on all 3 systems.
- Availability of macOS x64 runners on GitHub.
- MDCV/CLLI metadata exposed by OIIO 3.1 for HEIF/AVIF. OIIO exposes `CICP` in PNG, HEIF, JXL and FFmpeg `[code: OIIO v3.1.14.0]`; MDCV/CLLI were not found.
- JPEG XS and JPEG-LS support in vcpkg's FFmpeg 9 with an LGPL configuration.
- Whether OIIO 3 (vcpkg) still tags PFM as `Rec709` (D-17 handles the case either way).
- ~~Whether Windows runners create a D3D11 device and whether macOS runners have Metal~~: yes, both run the harness (run 12: "Microsoft Basic Render Driver" and Metal) `[test]`.
- Appearance of the BT.2390 EETF on real HDR photos on an SDR display: with the absolute peak as the source, the knee can end up very low (D-P10).
- HLG uses the OOTF of the nominal 1000-nit display (gamma 1.2) whatever the actual display; BT.2100 adjusts the gamma to the display peak `[code: src/color.cpp]`. Decision pending: D-P15.
- Whether OIIO 3.1 delivers the ICC profile of HEIF/AVIF files (without it, an AVIF with ICC and no CICP is treated as sRGB).
- Minimum Linux distribution for the `.tar.gz` (built on Ubuntu 24.04, glibc 2.39) and startup on pure Wayland (today: X11/XWayland).
- **Black menus and dialogs over the viewer with Vulkan on Xvfb + Mesa lavapipe:** Qt Widgets popups and dialogs shown over the viewer window are captured black (also in `v0.1-alpha`); they work (keyboard and clicks reach them) and render correctly with OpenGL, and a plain Widgets application with a Vulkan instance renders correctly `[test]`. Whether a real Linux desktop with a Vulkan driver shows the same is unknown (D-P14).
- Right-to-left layout of the overlay and of the side zones, checked by native Arabic/Urdu speakers; fonts for Indic scripts on minimal Linux installations.
- Qt's own dialogs (color picker, non-native file dialog on Linux) stay in English until Qt's translations (`qtbase_*.qm`) are deployed with the packages.
- The Ethnologue 2026 figures behind D-27 against ethnologue.com (only secondary sources were reachable).
- Undoing a move to the trash (0.2) restores the file by renaming it back and removes the trash's record of it (freedesktop `info/*.trashinfo`; the `$I…` file next to `$R…` in the Windows Recycle Bin). Check on Windows and macOS that the Recycle Bin and the Trash show no stale entry afterwards.
- Folder watching (0.2) on Windows: whether watching a folder stops the user from renaming or deleting that folder in Explorer while imageViewer shows it; and how network shares report changes.
- Known limits left after the 0.2 review (D-35), each with its reason:
  - Folder listing runs on the GUI thread: about 0.35 s by name and 0.5 s by date for 50,000 files `[test: review, Linux]`; ordinary folders are unaffected. Moving it to a worker (or skipping re-lists when nothing changed) moved to 0.4 with the formats-first 0.3 (D-40).
  - Closing the window waits for a decode that is running (seconds for a very large image).
  - The cache budget uses physical memory, not a container's (cgroup) limit.
  - A file replaced by another of the same size and modification time is not noticed by the cache.
  - Letter shortcuts on a keyboard with only a non-Latin layout (e.g. Russian alone) follow Qt's alternatives, which need a Latin layout configured as well (`us,ru` is the usual setup) `[test: xdotool, setxkbmap ru]`.
  - A Qt built without ICU (the local cloud build) falls back to POSIX collation: no natural order there; the official Qt binaries used by CI and the packages include ICU `[knowledge]`.
- Known limits left after the 0.3 review (D-35), each with its reason:
  - **EXR emissive pixels** (alpha 0 with colour, additive light) lose their colour: the pipeline holds straight alpha (D-21), which cannot represent them. Keeping premultiplied pixels from EXR to the shader would change the pipeline's invariant; it waits for a real need (VFX plates with additive elements).
  - **A slow decode in the worker cannot be cancelled:** navigation waits for it, at most 30 s (the worker's limit). The decode thread has no cancellation either (0.2 limit above).
  - **Camera RAW has no test file:** LibRaw refused every synthetic DNG we could write, and OpenImageIO then read it as a TIFF. A small real raw file with a licence that allows it (raw.pixls.us) is needed.
  - **Cineon** log values are read as if they were sRGB until OpenColorIO (Phase 5).
  - **XCF** per-pixel alpha is reduced to on/off by GraphicsMagick's tile reader `[code: coders/xcf.c]`; **CUT** palettes (`.pal` files) are not read.
  - **HEIC on Windows:** orientation and nclx colour through WIC are unverified (the CI runners have no HEIF/HEVC extensions); 10-bit and HDR HEIC are read as SDR everywhere.
  - **Technical labels stay untranslated:** the codec line of the information panel (for example `OpenImageIO/png (APNG)` or `ImageIO (macOS)`) names libraries and is what a bug report needs, word for word.
  - **Builds against an OpenImageIO older than 3.0** (Ubuntu 24.04 has 2.4.17) do not read Maya IFF (D-45); the packages use 3.1.
  - **The Settings dialog applies everything it shows:** an instance that opens it can set back a preference another instance changed after the first one started (D-44 covers the toggles).
- ~~Whether publishing a release in the web interface also fires the tag `push`~~: yes. The `release` event run gets canceled and the `push` run does the work (runs 18 and 19) `[test]`. The canceled run is harmless.

---

## Appendix A — Format matrix (goal D-07)

Sources of the lists:
- **qView:** Qt + kimageformats (`dist/linux/*.desktop`) `[code]`.
- **ImageGlass 10:** `IMAGE_FORMATS` in `Const.cs:83`, 86 extensions `[code]`.
- **FFmpeg image2:** `libavformat/img2.c`, 66 entries `[code: FFmpeg master]`.

Since 0.3 the registry (`imageViewer --formats`, D-38) is the list of what a build reads, with its decoder and test file; this table remains the goal. "0.3" in the last column: read since 0.3, with a test file decoded in CI.

| Family | Extensions | Backend | v1 |
|---|---|---|---|
| Web/consumer | jpg jpeg jpe jfif jfi, png, gif, webp, avif avifs, heic heif hif, jxl, bmp dib | OIIO; libjxl (jxl), libwebp (webp), the system (heic, D-39) | ✔ 0.3 (heic where the system decodes HEVC) |
| Animation | apng, gif, webp, avifs, jxl; gifv, mjpeg | own APNG reader, OIIO (gif), libwebp, libheif (avifs), libjxl; gifv, mjpeg: FFmpeg | ✔ 0.3, except gifv and mjpeg |
| Icons | ico, cur, icns, ani | OIIO (ico), Qt (cur, icns); ani: own parser, later | ✔ 0.3 (ani later) |
| Vector | svg svgz | Qt meanwhile (graphical interface only); lunasvg planned | ✔ 0.3 through Qt |
| Pro/VFX/cinema | exr, dpx, cin, hdr (Radiance), pic (Softimage), zfile, pfm phm, tif tiff (float, CMYK, multipage), psd psb, dds, sgi rgb rgba bw, tga, rla, fits, iff, jp2 j2k j2c jpc | OIIO | ✔ 0.3 (Cineon log as sRGB until OCIO, §14) |
| Professional JPEG | jls (JPEG-LS), jxs (JPEG XS), ljpg | FFmpeg (to verify) | ✔ if available |
| RAW | 3fr ari arw bay cap cr2 cr3 crw dcr dcs dng drf eip erf fff gpr iiq k25 kdc mdc mef mos mrw nef nrw orf pef ptx pxn raf raw rw2 rwl rwz sr2 srf srw x3f | OIIO/LibRaw | ✔ without a test file yet (§14) (r3d: RED SDK, out; gpr: to verify) |
| Simple bitmaps | pbm pgm ppm pnm pam, qoi, wbmp, xbm, xpm | OIIO (Netpbm), Qt (wbmp, xbm, xpm); qoi, pam: to add | ✔ 0.3, except qoi and pam |
| Legacy (FFmpeg) | pcx, ras sun sunras im1 im8 im24 im32 rs, xwd, pct pict, pcd, pix (Alias), img ximg timg (GEM), vbn, xface, cri, mng | GraphicsMagick in the decode worker (pcx, dcx, ras sun, pict pct, pix) since 0.3; the rest FFmpeg | partly ✔ 0.3 |
| Legacy (other) | wmf emf wpg viff xv cut fax, xcf, kra ora, flif, obm, exif; miff, dcm, vicar, mat, tim, mac, otb | GraphicsMagick in the decode worker (wpg, viff xv, cut, xcf, miff, dcm, vicar, mat, tim, mac, otb) since 0.3 (D-38); wmf, emf, fax: not yet; kra/ora: own (`mergedimage.png` in the zip), later | partly ✔ 0.3; flif obsolete |

---

## Appendix B — Evidence collected (2026-10-06)

**Measurements in Python (before D-05):**
- Frozen PySide6 startup: 0.21–0.28 s.
- Pillow truncates 16-bit PNG to 8 bits.
- Qt `QColorSpace` vs LittleCMS: mean ΔE00 of 0.034 (matrix/TRC profiles).
- Reading 24 MP into numpy: pyvips 0.57–0.87 s; Magick.NET via pythonnet 3.4–3.9 s.

**Qt 6.11 (qtbase `v6.11.2`, qtdeclarative 6.11)** `[code]`:
- `QRhiSwapChain::Format {SDR, HDR10, HDRExtendedSrgbLinear, HDRExtendedDisplayP3Linear}`.
- `QRhiSwapChainHdrInfo { LuminanceInNits | ColorComponentValue, sdrWhiteLevel }`.
- HDR backends on D3D11, D3D12, Vulkan and Metal.
- Widgets without HDR; Qt Quick with HDR only via `QSG_RHI_HDR`.

**C++ pipeline (local build, Qt 6.11.2 built from source + OIIO 2.4 from apt)** `[test]`:
- `tests/screen_test.py` under Xvfb: 8-bit sRGB ramp shown at 100 % with no difference at all from the file (0 levels, 0 % of pixels), with Vulkan/lavapipe and OpenGL/llvmpipe.
- Fused decoding (native → half): 2.5 MP JPEG in 54 ms; 24 MP JPEG in 0.9 s and 24 MP 16-bit PNG in 1.4 s, dominated by OIIO 2.4 reading. Peak memory ≈ 270 MB for 24 MP.
- Display P3 red (ICC) → scRGB (1.2246, −0.0421, −0.0196). The analytic value is (1.2249, −0.0421, −0.0196), identical within FP16 precision: the LittleCMS float transform with `NOOPTIMIZE` does not clip the gamut.
- EXR with value 4.0 is preserved.
- EXIF orientation 6 is applied exactly once.

**vcpkg:** versions and licenses in §5. There are no *ports* for ImageMagick, kimageformats, libultrahdr or resvg. exiv2 is GPL-2.
