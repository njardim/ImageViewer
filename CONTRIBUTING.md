# Contributing to imageViewer

Thank you for helping. imageViewer is licensed under the [Apache License 2.0](LICENSE); by contributing you agree that your contribution is licensed under it too.

## Developer Certificate of Origin

Every commit must be signed off, which certifies the [Developer Certificate of Origin](https://developercertificate.org/) (you wrote the change, or have the right to submit it under the project's license):

```
git commit -s -m "Describe the change"
```

This adds a `Signed-off-by: Your Name <you@example.com>` line. There is no separate contributor agreement.

## Before you start

- Read [`docs/PLAN.md`](docs/PLAN.md): the decisions (§2) and the fidelity criteria (§7) are the project's rules. To change a decision, open an issue with the new evidence first.
- [`CLAUDE.md`](CLAUDE.md) summarises the working rules. The most important ones:
  - **English only** in the repository: code, comments, interface text and documentation. The interface is translated separately (below).
  - **No code from GPL projects**: qView and ImageGlass are references for behaviour only.
  - **Dependencies** must be permissive or LGPL (dynamically linked); a CI gate checks every package.
  - **The color pipeline** converts once into linear scRGB at 16-bit float; the shader only applies exposure, tone mapping, compositing and the output encoding.
- Keep changes focused; one topic per pull request.

## Building and testing

See [Building from source](README.md#building-from-source). Before opening a pull request, run the tests that apply to your change:

- `tests/smoke.sh <executable>`: decoding and color, no display needed;
- `tests/render_test.py`, `tests/screen_test.py` and `tests/ui_test.py <executable> [vulkan|opengl]`: GPU output, on-screen pixels and interaction (Linux, under Xvfb);
- `tests/check_translations.py`: translations.

CI runs all of them on Windows, macOS and Linux.

## Translations

The interface uses Qt Linguist. Each language is a file in [`translations/`](translations), for example `imageviewer_pt.ts`. All languages except English were machine-translated and **need review by native speakers**: corrections are among the most valuable contributions.

1. Open the file in [Qt Linguist](https://doc.qt.io/qt-6/linguist-translators.html) (part of Qt's tools), or in any text editor (it is XML).
2. Correct the `<translation>` of each message. Keep `%1`, `%2`, … and `&&`, `;;`, `(*)` and HTML tags exactly as in the English source; `tests/check_translations.py` checks them.
3. Use the terms your operating system uses in that language (for example the name of the Recycle Bin or the Trash).
4. Open a pull request, saying which language you reviewed. When a language has been reviewed completely, we remove the "machine translation" note for it.

To propose a new language, open an issue first.

When you change interface text in the code, regenerate the files with `cmake --build <build directory> --target update_translations` and say so in the pull request; the new strings are then translated before the next release, which requires every language to be complete.

## Reporting problems

Open an issue with:
- the version (`imageViewer --version`) and your system;
- for color problems, the output of `imageViewer --info <file>` and, if possible, the file;
- for HDR problems, the "Output" and "Highlights" rows of the information panel (press I).
