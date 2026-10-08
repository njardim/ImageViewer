// The format registry (decision D-38): every format imageViewer reads, how it is recognised
// from the file's first bytes, which decoder reads it and what it can hold. Detection, the
// Open dialog filter, folder listing, `--formats` and the CI format check all come from it.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>

enum class Decoder {
    OpenImageIO, // most still formats, RAW (LibRaw), EXR, DPX, AVIF (libheif)
    Jxl,         // libjxl
    WebP,        // libwebp: ICC profiles and animation
    Qt,          // QImageReader plugins (ICNS, XPM, SVG...)
};

enum FormatCapability : unsigned {
    CanHdr = 1u << 0,       // values above SDR white (float, PQ, HLG)
    CanAlpha = 1u << 1,
    CanAnimate = 1u << 2,
    CanHavePages = 1u << 3, // several images in one file
};

struct Format {
    const char *id;         // stable key, e.g. "jpegxl"
    const char *name;       // as written in --formats and the README, e.g. "JPEG XL"
    const char *extensions; // space separated, lower case, the usual one first
    Decoder decoder;
    unsigned capabilities;  // FormatCapability flags
    // True when `head` (the file's first bytes) has the format's signature; nullptr for
    // formats without one (TGA, RLA), which are known by their extension only.
    bool (*signature)(QByteArrayView head);
    // Other formats are built on this one (TIFF: most camera RAW files), so when both match,
    // the extension decides.
    bool container;
    const char *testFile;   // in tests/data, decoded by CI on the 3 systems; nullptr: not yet

    QStringList suffixes() const;
};

// Every format, in detection order (specific signatures before the containers they share).
const QList<Format> &formats();

// Whether this build can read the format: its library is linked and, for OpenImageIO and
// Qt, the plugin is present at run time.
bool isAvailable(const Format &format);

// How many bytes detectFormat() wants from the start of the file.
inline constexpr qsizetype kFormatHeadBytes = 64 * 1024;

// The format of a file: by its first bytes and, between candidates (or for formats without
// a signature), by its suffix. nullptr when nothing matches: decoders then try in turn.
const Format *detectFormat(QByteArrayView head, const QString &suffix);

// Lower-case suffixes (without dot) of every available format, sorted.
const QStringList &supportedSuffixes();

// `--formats`: one line per format, "id | name | decoder | yes/no | capabilities | extensions |
// test file" (tests/smoke.sh decodes every test file).
QStringList formatReport();
