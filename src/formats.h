// The format registry (decision D-38): every format ImageViewer reads, how it is recognised
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
    Apng,        // animated PNG: frames rebuilt as PNGs for OpenImageIO, composited here
    Softimage,   // our own Softimage PIC reader (D-45): OpenImageIO's crashes on damaged files
    Heif,        // libheif's track API: AVIF image sequences (stills go through OpenImageIO)
    Qt,          // QImageReader plugins (ICNS, XPM, SVG...)
    GraphicsMagick, // the long tail, in the decode worker process (worker.cpp); coder = id in upper case
    System,      // the operating system's decoders (D-39): HEIC through ImageIO (macOS) or WIC (Windows)
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
// Qt, the plugin is present at run time; for the System decoder, the system has the codec.
bool isAvailable(const Format &format);

// False for a format whose OpenImageIO reader in this build crashes on damaged files (D-45):
// such a file never reaches OpenImageIO, not even as a fallback.
bool oiioMayRead(const Format &format);
// False when the file's first bytes are those of such a format, whatever format was detected:
// OpenImageIO picks its reader by content, so a Softimage PIC named .svg reached it as a fallback.
bool oiioMayReadContent(QByteArrayView head);

// How many bytes detectFormat() wants from the start of the file.
inline constexpr qsizetype kFormatHeadBytes = 64 * 1024;

// The format of a file: by its first bytes and, between candidates (or for formats without
// a signature), by its suffix. nullptr when nothing matches: decoders then try in turn.
const Format *detectFormat(QByteArrayView head, const QString &suffix);

// Lower-case suffixes (without dot) of every available format, sorted. Formats the system
// decodes are listed even where its codec is missing, so that opening one says how to add it.
const QStringList &supportedSuffixes();

// `--formats`: one line per format, "id | name | decoder | availability | capabilities |
// extensions | test file", availability being yes, no or system-missing (the System decoder
// without the system's codec). tests/smoke.sh decodes every test file.
QStringList formatReport();
