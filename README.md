# ImageViewer

## Vision and scope

ImageViewer is a cross-platform image viewer (Windows, macOS, Linux). It is minimalist and fast, and has **demonstrable image fidelity in SDR and HDR**.

What sets us apart comes down to three things:
1. a correct and verifiable color and HDR pipeline;
2. professional-grade format coverage (EXR, DPX, RAW, PQ, HLG, etc..);
3. a clutter-free interface.

ImageViewer shows images the way they were made. Every image is converted once, from its own color description (ICC profile or CICP code points), into a linear wide-gamut working space at 16-bit floating point. It is then sent to the display through a real HDR output where the system offers one: scRGB on Windows, EDR on macOS, HDR10 where available. Highlights brighter than the display are tone mapped with ITU-R BT.2390, or clipped on request, and the viewer always tells you which. Fidelity is tested automatically on every build, not just claimed (see [How fidelity is verified](#how-fidelity-is-verified)).

The interface stays out of the way: the image fills the window, everything else is a right-click or a key away, and it speaks 37 languages.

> Status: **0.3** — early releases. The color pipeline, HDR output and the essential viewer functions are in place; see the [plan](docs/PLAN.md) for what comes next.

## Download

Get the latest version from [Releases](https://github.com/njardim/ImageViewer/releases):

| System | Package | Requirements |
|---|---|---|
| Windows x64 | `ImageViewer-<version>-windows-x64.zip` — unzip and run `bin/ImageViewer.exe` | Windows 10 (22H2) or 11 |
| macOS (Apple silicon) | `ImageViewer-<version>-macos-arm64.dmg` — drag `ImageViewer.app` to Applications | macOS 14.4 or later |
| Linux x64 | `ImageViewer-<version>-linux-x64.tar.gz` — extract and run `bin/ImageViewer` | glibc 2.39 (Ubuntu 24.04 or newer distributions), X11 or XWayland, `libopengl0`, `libegl1`, `libxcb-cursor0` |

`SHA256SUMS` lists the checksums of the packages. Each package is built and tested by CI on a clean machine, and on public releases it carries a GitHub build provenance attestation (`gh attestation verify <file> --repo njardim/ImageViewer`).

The packages are **not signed yet**:
- **Windows:** SmartScreen asks for confirmation. Choose *More info → Run anyway*.
- **macOS:** Gatekeeper blocks the first launch. Open *System Settings → Privacy & Security* and choose *Open Anyway*.

## Features (0.4)

- **Color-managed decoding:** ICC v2/v4 profiles, CICP (PQ, HLG, BT.709/BT.2020, linear), EXR chromaticities; 8- and 16-bit integer and 16/32-bit floating-point images. Wide-gamut colors are kept, not clipped to sRGB.
- **HDR output:** scRGB (Windows), EDR (macOS) and HDR10/PQ, with absolute luminance for PQ content. BT.2390 tone mapping only when an image exceeds the display; exposure control; an option to highlight clipped or tone-mapped pixels.
- **Formats:** JPEG, JPEG XL, PNG and APNG, TIFF, WebP, AVIF, GIF, JPEG 2000, OpenEXR, DPX, Cineon, PFM, Radiance HDR, BMP, TGA, PSD (composite), DDS, camera RAW (LibRaw) and more; animated GIF, WebP, APNG, JPEG XL and AVIF. The long tail — GIMP XCF, PCX/DCX, Apple PICT, WordPerfect WPG, MIFF, Sun raster, Khoros VIFF, DICOM, VICAR, MATLAB, PlayStation TIM, Dr. Halo CUT, MacPaint, Alias PIX, Nokia OTB — is read by GraphicsMagick 1.3.49 in a separate process with a list of allowed decoders, limits and a time-out, so a damaged file there cannot take the viewer down. `ImageViewer --formats` lists what a build reads, and with which library. Files are recognised by their content, not their extension. HEIC is decoded by the operating system: macOS always; Windows with Microsoft's “HEIF Image Extensions” and “HEVC Video Extensions” from the Microsoft Store; on Linux, the package reads no HEIC (its libheif has no HEVC decoder), while a build from source against the distribution's libheif reads it when that libheif has an HEVC decoder (e.g. `libheif-plugin-libde265`). ImageViewer ships no HEVC decoder (patents).
- **Animation:** GIF, WebP, APNG, JPEG XL and AVIF sequences play with their own timing and loop count, as browsers play them; pause and step frame by frame.
- **Viewing:**
  - zoom at the cursor, exact 100 % (one image pixel per screen pixel), pan;
  - a new image fits the window, its width or its height, or fills the window; small images stay at 100 % unless you choose to enlarge them; Lock Zoom keeps the zoom for the next images;
  - the window can take the size of the first image or of every image; the title bar shows as much as you choose (name, position, dimensions, file size, zoom);
  - rotate and mirror horizontally or vertically;
  - full screen; an optional checkerboard behind transparent areas;
  - a slideshow through the folder.
- **Information:** one panel (I) with everything about the image: file, dimensions, format, color description, peak, camera data (EXIF), view and output, including whether highlights are tone mapped or clipped. A compact line at the top (Shift+I) shows the file name, dimensions, file size, zoom, color space and date, configurable and always shown in full screen; it never moves the image. A still pointer hides after 2 s. Both share one look (background opacity, text opacity, outline) and stay legible over any image, in SDR and HDR.
- **Navigation:** folders sorted by name (natural order), date or size, ascending or descending; previous/next by keyboard, mouse side buttons or the clickable sides of the window; first and last image; optional looping. The next and previous images are loaded in advance, so stepping either way is immediate. The folder is watched: files added, removed or changed by other programs show up at once.
- **File actions:**
  - copy the image (16-bit sRGB bitmap plus the file) or its path;
  - rename; move to the trash or Recycle Bin (with a confirmation you can turn off) and undo it; delete permanently (always asks);
  - open recent files; show the file in Explorer, Finder or your file manager;
  - open it with another application: the ones your system offers for the file, its default first.
- **Settings** (with Apply): language, background color and checkerboard, window size and position remembered, zoom of a new image, window size and title bar, information panel and top overlay, side zones, sorting, preloading, slideshow interval, display output (automatic, SDR, HDR10), tone mapping, and every keyboard shortcut.
- **37 interface languages:** English, the most spoken languages (简体中文, हिन्दी, Español, العربية, Français, বাংলা, Português, Bahasa Indonesia, اردو, Русский, Deutsch, 日本語, मराठी, Tiếng Việt, తెలుగు), 한국어, Italiano, Türkçe and every official language of the European Union (Български, Hrvatski, Čeština, Dansk, Nederlands, Eesti, Suomi, Ελληνικά, Magyar, Gaeilge, Latviešu, Lietuvių, Malti, Polski, Română, Slovenčina, Slovenščina, Svenska). All languages except English are machine translations awaiting review by native speakers; [corrections are welcome](CONTRIBUTING.md#translations).

## Using it

`ImageViewer [file or folder]`, drag and drop, or *Open* from the right-click menu.

| Key | Action |
|---|---|
| ← → (or click the window's sides) | Previous / next image |
| Shift+← / Shift+→ (or Home / End) | First / last image |
| + / − / mouse wheel | Zoom |
| 0 / 1 | Fit to window / 100 % |
| W / H | Fit to width / height |
| Z | Lock the zoom for the next images |
| F, F11, double-click | Full screen |
| R / Shift+R | Rotate clockwise / counterclockwise |
| Shift+W / Shift+H | Mirror horizontally / vertically |
| I / Shift+I | Information panel / overlay at the top |
| B | Checkerboard behind transparent areas |
| Space / Ctrl+← / Ctrl+→ (⌘ on macOS) | Pause an animation (or stop a slideshow) / previous frame / next frame |
| S / Esc | Start or stop the slideshow / stop it |
| E / Shift+E / Ctrl+E | Exposure +½ / −½ EV / reset |
| T | Tone mapping on/off (off: clip at the display's peak) |
| C | Highlight altered (clipped or tone-mapped) pixels |
| Ctrl+C / Ctrl+Shift+C | Copy image / copy file path |
| F2 or Return | Rename |
| Delete (⌘⌫ on macOS) / Ctrl+Z | Move to the trash / undo |
| Shift+Delete (⌘⇧⌫ on macOS) | Delete permanently |
| Ctrl+, | Settings |
| Q (or Ctrl+Q) | Quit |
| Right-click | Menu with every command |

On macOS, Ctrl is ⌘. Every shortcut can be changed in *Settings → Shortcuts*. The defaults follow one rule: single keys view and navigate, Shift + a key does that key's other command, ⌘/Ctrl does the application's own commands; none needs the fn key of a MacBook.

## How fidelity is verified

On every change, CI builds and tests on Windows, macOS and Linux (the on-screen and interaction tests need a display server, so they run on Linux, under Xvfb):
- **Decode and color tests:** a Display P3 red keeps its out-of-sRGB value (also in WebP), an EXR keeps values above SDR white, a 16-bit JPEG XL keeps all 16 bits and a PQ JPEG XL its 1,000 nits, alpha is handled correctly, every format in the registry that has a test file decodes it (camera RAW has none yet), and every frame of the test animations has the right color and duration. The same tests run again on the packaged application.
- **Damaged files:** every test file, corrupted in a dozen ways, must decode or fail cleanly within a time limit, without crashing or hanging the viewer.
- **Output tests:** the GPU output is read back for SDR, EDR, scRGB and HDR10, with and without tone mapping, and compared with:
  - a CPU reference implementation;
  - the BT.2390 specification, computed independently: identity below the knee, the peak landing on the display peak, monotonic output, hues preserved.
- **On-screen test:** an 8-bit image shown at 100 % must be identical to the file, bit for bit.
- **Interaction test:** a user is simulated clicking the window's sides, flipping and rotating the image (compared pixel for pixel), moving a file to the trash and back, renaming, deleting permanently, reacting to files changed by another program, showing the top overlay without moving the image by a pixel, applying settings without closing them, playing, pausing and stepping an animation, running the slideshow, and quitting with the session saved. On macOS, quitting from the application menu is tested too.

[`docs/PLAN.md`](docs/PLAN.md) lists the fidelity criteria (§7), the decisions behind them (§2) and the test results (§1).

## Building from source

The dependencies are Qt 6.12 (official binaries) and OpenImageIO, LittleCMS, libjxl, libwebp, libheif, GraphicsMagick and their codecs (from vcpkg, using the manifest `vcpkg.json`).

```
cmake --preset <windows|macos|linux> -DCMAKE_PREFIX_PATH=<Qt 6.12 directory>   # needs VCPKG_ROOT
cmake --build --preset <windows|macos|linux>
tests/smoke.sh <executable>
```

To develop on Linux without vcpkg, run `scripts/build-qt-linux.sh` (builds Qt from source) and use the `linux-system` preset. `CLAUDE.md` describes the full test set and the rules for working on the code.

Command-line tools for diagnosis:
- `ImageViewer --info <file>` prints what the color pipeline sees.
- `ImageViewer --render <file> --output sdr|edr|scrgb|pq` draws the image off screen and compares the GPU with the CPU reference.

## Releases

Versions are numbered `X.Y` (for example 0.1, 0.2, 1.0), with an optional suffix such as `-beta`. Publishing a GitHub release named `vX.Y` builds, tests and attaches the three packages automatically; nobody builds release binaries by hand.

## Contributing

Contributions are welcome, including translations; see [`CONTRIBUTING.md`](CONTRIBUTING.md). Commits need a [Developer Certificate of Origin](https://developercertificate.org/) sign-off (`git commit -s`).

## License

[Apache License 2.0](LICENSE). See [`NOTICE`](NOTICE). The name and logo "Cristallumnis" are trademarks of Cristallumnis, Lda. and are not licensed under the Apache License. The packages include the licenses of the third-party components in their `third-party` folder.

© 2026 Cristallumnis, Lda.
