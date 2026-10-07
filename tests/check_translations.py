#!/usr/bin/env python3
"""Consistency gate for the user-interface translations (decision D-27).

For every translations/imageviewer_<lang>.ts except the source language (English):
  - a finished translation must keep the source's %N placeholders (same multiset), and
    its "&&" (a literal "&"), ";;" and "(*)" (file-dialog filters) and HTML tags;
  - unfinished or empty translations are listed. Qt shows the English source for them,
    so during development they are warnings; with --require-complete (releases) they fail.

usage: python3 tests/check_translations.py [--require-complete] [translations-dir]
Standard library only, so it runs unchanged on every CI runner.
"""
import collections
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

# Qt's arg() markers run from %1 to %99: "%100" (Turkish for "100 %") is text, not a placeholder.
PLACEHOLDER = re.compile(r"%L?[1-9][0-9]?(?![0-9])|%n")
MARKERS = ("&&", ";;", "(*)")
TAG = re.compile(r"</?[a-zA-Z][^>]*>")


def signature(text):
    return (collections.Counter(PLACEHOLDER.findall(text)), {m: text.count(m) for m in MARKERS},
            collections.Counter(TAG.findall(text)))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    require_complete = "--require-complete" in sys.argv[1:]
    directory = pathlib.Path(args[0] if args else pathlib.Path(__file__).resolve().parent.parent / "translations")
    files = sorted(directory.glob("imageviewer_*.ts"))
    if not files:
        sys.exit(f"error: no translation files in {directory}")
    errors, warnings = [], []
    for path in files:
        root = ET.parse(path).getroot()
        language = root.get("language", "?")
        if language == root.get("sourcelanguage", "en"):
            continue  # the source language's catalog carries plural forms only
        finished = unfinished = 0
        for context in root.iter("context"):
            name = context.findtext("name")
            for message in context.iter("message"):
                source = message.findtext("source") or ""
                translation = message.find("translation")
                forms = translation.findall("numerusform") if translation is not None else []
                texts = [f.text or "" for f in forms] if forms else [translation.text or ""] if translation is not None else []
                if translation is None or translation.get("type") in ("unfinished", "vanished", "obsolete") \
                        or not any(t.strip() for t in texts):
                    unfinished += 1
                    continue
                finished += 1
                expected = signature(source)
                for text in texts:
                    if signature(text) != expected:
                        errors.append(f"{path.name} [{name}] {source!r} -> {text!r}: placeholders, '&&', ';;', "
                                      f"'(*)' or HTML tags differ from the source")
        line = f"{language:6} {finished:4} translated, {unfinished:4} unfinished"
        print(line)
        if unfinished:
            (errors if require_complete else warnings).append(f"{path.name}: {unfinished} unfinished message(s)")
    for message in warnings:
        print(f"warning: {message}")
    for message in errors:
        print(f"error: {message}")
    if errors:
        sys.exit(1)
    print("translations: consistent" + (", complete" if require_complete else ""))


if __name__ == "__main__":
    main()
