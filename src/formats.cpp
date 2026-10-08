#include "formats.h"

#include "decoders.h"

#include <OpenImageIO/imageio.h>

#include <QImageReader>
#include <QSet>

#include <algorithm>
#include <cstring>

namespace {

bool startsWith(QByteArrayView head, const char *magic, qsizetype at = 0)
{
    const qsizetype n = qsizetype(std::strlen(magic));
    return head.size() >= at + n && std::memcmp(head.data() + at, magic, std::size_t(n)) == 0;
}

bool startsWithBytes(QByteArrayView head, std::initializer_list<unsigned char> magic, qsizetype at = 0)
{
    if (head.size() < at + qsizetype(magic.size()))
        return false;
    return std::equal(magic.begin(), magic.end(), reinterpret_cast<const unsigned char *>(head.data()) + at);
}

quint32 bigEndian32(QByteArrayView head, qsizetype at)
{
    const auto *p = reinterpret_cast<const unsigned char *>(head.data()) + at;
    return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | quint32(p[3]);
}

// ISO base media file (HEIF, AVIF): the "ftyp" box lists a major brand and compatible ones.
bool hasBrand(QByteArrayView head, std::initializer_list<const char *> brands)
{
    if (head.size() < 16 || !startsWith(head, "ftyp", 4))
        return false;
    const qsizetype boxEnd = std::min<qsizetype>(head.size(), bigEndian32(head, 0));
    for (qsizetype at = 8; at + 4 <= boxEnd; at += at == 8 ? 8 : 4) // skip the minor version after the major brand
        for (const char *brand : brands)
            if (startsWith(head, brand, at))
                return true;
    return false;
}

bool isJpegXl(QByteArrayView h)
{
    return startsWithBytes(h, {0xFF, 0x0A})
           || startsWithBytes(h, {0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A});
}
bool isWebP(QByteArrayView h) { return startsWith(h, "RIFF") && startsWith(h, "WEBP", 8); }
bool isAvif(QByteArrayView h) { return hasBrand(h, {"avif", "avis"}); }
bool isAvifSequence(QByteArrayView h) { return hasBrand(h, {"avis"}); }
bool isHeic(QByteArrayView h) { return hasBrand(h, {"heic", "heix", "heim", "heis", "hevc", "hevx", "hevm", "hevs"}); }
// The long tail, read by GraphicsMagick in the decode worker (D-38).
bool isPcx(QByteArrayView h)
{
    if (h.size() < 4)
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(h.data());
    return p[0] == 0x0A && (p[1] == 0 || (p[1] >= 2 && p[1] <= 5)) && p[2] <= 1
           && (p[3] == 1 || p[3] == 2 || p[3] == 4 || p[3] == 8);
}
bool isDcx(QByteArrayView h) { return startsWithBytes(h, {0xB1, 0x68, 0xDE, 0x3A}); }
// After the 512-byte header: the version opcode of PICT 2 or PICT 1.
bool isPict(QByteArrayView h) { return startsWithBytes(h, {0x00, 0x11, 0x02, 0xFF}, 522) || startsWithBytes(h, {0x11, 0x01}, 522); }
bool isXcf(QByteArrayView h) { return startsWith(h, "gimp xcf "); }
bool isSunRaster(QByteArrayView h) { return startsWithBytes(h, {0x59, 0xA6, 0x6A, 0x95}); }
bool isViff(QByteArrayView h) { return startsWithBytes(h, {0xAB, 0x01}); }
bool isMiff(QByteArrayView h) { return startsWith(h, "id=ImageMagick"); }
bool isDicom(QByteArrayView h) { return startsWith(h, "DICM", 128); }
bool isTim(QByteArrayView h)
{
    if (!startsWithBytes(h, {0x10, 0x00, 0x00, 0x00}) || h.size() < 8)
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(h.data());
    return (p[4] <= 3 || p[4] == 8 || p[4] == 9) && p[5] == 0 && p[6] == 0 && p[7] == 0;
}
bool isVicar(QByteArrayView h) { return startsWith(h, "LBLSIZE="); }
bool isMatlab(QByteArrayView h) { return startsWith(h, "MATLAB 5.0 MAT-file"); }
bool isWpg(QByteArrayView h) { return startsWithBytes(h, {0xFF, 'W', 'P', 'C'}); }
bool isPng(QByteArrayView h) { return startsWithBytes(h, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}); }
// A PNG whose animation control chunk comes before its image data.
bool isApng(QByteArrayView h)
{
    if (!isPng(h))
        return false;
    for (qsizetype at = 8; at + 8 <= h.size();) {
        if (startsWith(h, "acTL", at + 4))
            return true;
        if (startsWith(h, "IDAT", at + 4))
            return false;
        const quint32 length = bigEndian32(h, at);
        if (length > quint32(h.size()))
            return false;
        at += qsizetype(length) + 12;
    }
    return false;
}
bool isJpeg(QByteArrayView h) { return startsWithBytes(h, {0xFF, 0xD8, 0xFF}); }
bool isGif(QByteArrayView h) { return startsWith(h, "GIF87a") || startsWith(h, "GIF89a"); }
bool isExr(QByteArrayView h) { return startsWithBytes(h, {0x76, 0x2F, 0x31, 0x01}); }
bool isJpeg2000(QByteArrayView h)
{
    return startsWithBytes(h, {0x00, 0x00, 0x00, 0x0C, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A})
           || startsWithBytes(h, {0xFF, 0x4F, 0xFF, 0x51});
}
bool isDpx(QByteArrayView h) { return startsWith(h, "SDPX") || startsWith(h, "XPDS"); }
bool isCineon(QByteArrayView h)
{
    return startsWithBytes(h, {0x80, 0x2A, 0x5F, 0xD7}) || startsWithBytes(h, {0xD7, 0x5F, 0x2A, 0x80});
}
bool isRadiance(QByteArrayView h) { return startsWith(h, "#?RADIANCE") || startsWith(h, "#?RGBE"); }
bool isPsd(QByteArrayView h) { return startsWith(h, "8BPS"); }
bool isDds(QByteArrayView h) { return startsWith(h, "DDS "); }
bool isBmp(QByteArrayView h) { return startsWith(h, "BM") && h.size() >= 26; }
// An icon directory: type 1 (icon) or 2 (cursor), at least one entry whose reserved byte is
// 0 and whose image starts after the directory. An uncompressed true-colour TGA without an
// ID starts with the same 00 00 02 00, but its next two bytes (the colour map's first
// index) are 0, which no directory has.
bool isIconDirectory(QByteArrayView h, unsigned char type)
{
    if (!startsWithBytes(h, {0x00, 0x00, type, 0x00}) || h.size() < 22)
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(h.data());
    const unsigned count = unsigned(p[4]) | unsigned(p[5]) << 8;
    const quint32 offset = quint32(p[18]) | quint32(p[19]) << 8 | quint32(p[20]) << 16 | quint32(p[21]) << 24;
    return count >= 1 && p[9] == 0 && offset >= 6 + 16 * count;
}
bool isIco(QByteArrayView h) { return isIconDirectory(h, 0x01); }
bool isCur(QByteArrayView h) { return isIconDirectory(h, 0x02); }
bool isFits(QByteArrayView h) { return startsWith(h, "SIMPLE  ="); }
bool isPnm(QByteArrayView h)
{
    if (h.size() < 3 || h[0] != 'P')
        return false;
    const char kind = h[1], next = h[2];
    const bool space = next == ' ' || next == '\n' || next == '\r' || next == '\t';
    return space && ((kind >= '1' && kind <= '6') || kind == 'F' || kind == 'f');
}
bool isSgi(QByteArrayView h) { return startsWithBytes(h, {0x01, 0xDA}); }
bool isSoftimage(QByteArrayView h) { return startsWithBytes(h, {0x53, 0x80, 0xF6, 0x34}); }
bool isMayaIff(QByteArrayView h) { return startsWith(h, "FOR4") && startsWith(h, "CIMG", 8); }
bool isZfile(QByteArrayView h)
{
    return startsWithBytes(h, {0x2F, 0x08, 0x67, 0xAB}) || startsWithBytes(h, {0xAB, 0x67, 0x08, 0x2F});
}
bool isTiff(QByteArrayView h)
{
    return startsWith(h, "II*") || startsWithBytes(h, {'M', 'M', 0x00, '*'}) || startsWith(h, "II+")
           || startsWithBytes(h, {'M', 'M', 0x00, '+'});
}
bool isSvg(QByteArrayView h)
{
    const QByteArrayView start = h.first(std::min<qsizetype>(h.size(), 1024));
    return start.indexOf("<svg") >= 0 || startsWithBytes(h, {0x1F, 0x8B}); // svgz is gzip
}
bool isIcns(QByteArrayView h) { return startsWith(h, "icns"); }
bool isXpm(QByteArrayView h) { return startsWith(h, "/* XPM */"); }

using D = Decoder;

// Detection order: specific signatures first; TIFF last among the signatures because camera
// RAW files share it.
const QList<Format> kFormats = {
    {"jpegxl", "JPEG XL", "jxl", D::Jxl, CanHdr | CanAlpha | CanAnimate, isJpegXl, false, "gradient.jxl"},
    {"webp", "WebP", "webp", D::WebP, CanAlpha | CanAnimate, isWebP, false, "lossless.webp"},
    {"avifs", "AVIF sequence", "avif avifs", D::Heif, CanHdr | CanAlpha | CanAnimate, isAvifSequence, false, "anim.avif"},
    {"avif", "AVIF", "avif avifs", D::OpenImageIO, CanHdr | CanAlpha, isAvif, false, "flat.avif"},
    // Read through Qt where Qt has a HEIF plugin (macOS: the system's ImageIO); D-39 replaces it.
    {"heic", "HEIC/HEIF", "heic heif hif heics", D::System, CanAlpha, isHeic, false, "orange.heic"},
    {"apng", "Animated PNG", "png apng", D::Apng, CanHdr | CanAlpha | CanAnimate, isApng, false, "anim.png"},
    {"png", "PNG", "png", D::OpenImageIO, CanHdr | CanAlpha, isPng, false, "alpha8.png"},
    {"jpeg", "JPEG", "jpg jpeg jpe jfif jfi jif", D::OpenImageIO, 0, isJpeg, false, "orient6.jpg"},
    {"gif", "GIF", "gif", D::OpenImageIO, CanAlpha | CanAnimate, isGif, false, "palette.gif"},
    {"openexr", "OpenEXR", "exr sxr mxr", D::OpenImageIO, CanHdr | CanAlpha | CanHavePages, isExr, false, "hdr4.exr"},
    {"jpeg2000", "JPEG 2000 / HTJ2K", "jp2 j2k j2c jph", D::OpenImageIO, CanAlpha, isJpeg2000, false, "lossless.jp2"},
    {"dpx", "DPX", "dpx", D::OpenImageIO, CanHdr | CanAlpha, isDpx, false, "orange.dpx"},
    {"cineon", "Cineon", "cin", D::OpenImageIO, 0, isCineon, false, "gm.cin"},
    {"hdr", "Radiance HDR", "hdr rgbe", D::OpenImageIO, CanHdr, isRadiance, false, "orange.hdr"},
    {"psd", "Photoshop (composite)", "psd psb pdd", D::OpenImageIO, CanAlpha, isPsd, false, "gm.psd"},
    {"dds", "DirectDraw Surface", "dds", D::OpenImageIO, CanAlpha, isDds, false, "orange.dds"},
    {"bmp", "BMP", "bmp dib", D::OpenImageIO, CanAlpha, isBmp, false, "orange.bmp"},
    {"ico", "Windows icon", "ico", D::OpenImageIO, CanAlpha | CanHavePages, isIco, false, "orange.ico"},
    {"cur", "Windows cursor", "cur", D::Qt, CanAlpha, isCur, false, "orange.cur"},
    {"fits", "FITS", "fits", D::OpenImageIO, CanHdr, isFits, false, "orange.fits"},
    {"pnm", "Netpbm (PBM, PGM, PPM, PFM)", "ppm pgm pbm pnm pfm", D::OpenImageIO, CanHdr, isPnm, false, "rows2.pfm"},
    {"sgi", "SGI", "sgi rgb rgba bw int inta", D::OpenImageIO, CanAlpha, isSgi, false, "orange.sgi"},
    {"softimage", "Softimage PIC", "pic", D::OpenImageIO, CanAlpha, isSoftimage, false, "orange.pic"},
    {"iff", "Maya IFF", "iff z", D::OpenImageIO, CanAlpha, isMayaIff, false, "orange.iff"},
    {"zfile", "Pixar zfile", "zfile", D::OpenImageIO, CanHdr, isZfile, false, "gray.zfile"},
    {"icns", "Apple icon", "icns", D::Qt, CanAlpha, isIcns, false, "orange.icns"},
    {"xpm", "XPM", "xpm", D::Qt, CanAlpha, isXpm, false, "gm.xpm"},
    {"svg", "SVG", "svg svgz", D::Qt, CanAlpha, isSvg, false, "orange.svg"},
    {"tiff", "TIFF", "tif tiff tx env sm vsm", D::OpenImageIO, CanHdr | CanAlpha | CanHavePages, isTiff, true, "rgb16.tif"},
    // Without a signature of their own: known by their extension.
    {"raw", "Camera RAW (LibRaw)",
     "dng cr2 cr3 crw nef nrw arw srf sr2 raf orf rw2 rwl pef srw x3f 3fr fff iiq cap eip mef mos mrw "
     "kdc dcr k25 erf bay bmq cs1 dc2 drf dsc ia kc2 mdc ptx pxn qtk raw rdc rwz sti cine",
     D::OpenImageIO, 0, nullptr, false, nullptr},
    {"targa", "Targa", "tga tpic", D::OpenImageIO, CanAlpha, nullptr, false, "orange.tga"},
    {"rla", "Wavefront RLA", "rla", D::OpenImageIO, CanAlpha, nullptr, false, "orange.rla"},
    {"xbm", "XBM", "xbm", D::Qt, 0, nullptr, false, "gm.xbm"},
    // The long tail (D-38): GraphicsMagick in the decode worker; the coder is the id in upper
    // case and must be in worker.cpp's allow-list.
    {"xcf", "GIMP XCF", "xcf", D::GraphicsMagick, CanAlpha, isXcf, false, "layers.xcf"},
    {"pcx", "PCX (ZSoft Paintbrush)", "pcx", D::GraphicsMagick, 0, isPcx, false, "gm.pcx"},
    {"dcx", "DCX (multi-page PCX)", "dcx", D::GraphicsMagick, CanHavePages, isDcx, false, "gm.dcx"},
    {"pict", "PICT (Apple QuickDraw)", "pict pct", D::GraphicsMagick, 0, isPict, false, "gm.pict"},
    {"wpg", "WordPerfect Graphics", "wpg", D::GraphicsMagick, 0, isWpg, false, "gm.wpg"},
    {"miff", "MIFF (Magick)", "miff mif", D::GraphicsMagick, CanAlpha | CanHavePages, isMiff, false, "gm.miff"},
    {"sun", "Sun raster", "ras sun", D::GraphicsMagick, CanAlpha, isSunRaster, false, "gm.ras"},
    {"viff", "Khoros VIFF", "viff xv", D::GraphicsMagick, CanAlpha, isViff, false, "gm.viff"},
    {"dcm", "DICOM", "dcm dicom", D::GraphicsMagick, CanHavePages, isDicom, false, "gray16.dcm"},
    {"vicar", "VICAR", "vicar vic", D::GraphicsMagick, 0, isVicar, false, "gm.vicar"},
    {"mat", "MATLAB", "mat", D::GraphicsMagick, 0, isMatlab, false, "gm.mat"},
    {"tim", "PlayStation TIM", "tim", D::GraphicsMagick, CanAlpha, isTim, false, "rgb.tim"},
    {"cut", "Dr. Halo CUT", "cut", D::GraphicsMagick, 0, nullptr, false, "gray.cut"},
    {"mac", "MacPaint", "mac", D::GraphicsMagick, 0, nullptr, false, "bw.mac"},
    {"pix", "Alias PIX", "pix", D::GraphicsMagick, 0, nullptr, false, "rgb.pix"},
    {"otb", "Nokia OTA bitmap", "otb", D::GraphicsMagick, 0, nullptr, false, "gm.otb"},
    {"wbmp", "Wireless bitmap", "wbmp", D::Qt, 0, nullptr, false, "gm.wbmp"},
};

const char *decoderName(Decoder d)
{
    switch (d) {
    case Decoder::OpenImageIO: return "OpenImageIO";
    case Decoder::Jxl: return "libjxl";
    case Decoder::WebP: return "libwebp";
    case Decoder::Apng: return "APNG";
    case Decoder::Heif: return "libheif";
    case Decoder::Qt: return "Qt";
    case Decoder::System: return "system";
    case Decoder::GraphicsMagick: return "GraphicsMagick";
    }
    return "?";
}

// The suffixes the linked OpenImageIO and Qt plugins claim.
const QSet<QString> &oiioSuffixes()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        // "fmt:ext,ext;fmt:ext", one entry per plugin built into this binary.
        const QString list = QString::fromStdString(OIIO::get_string_attribute("extension_list"));
        for (const QString &entry : list.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            for (const QString &ext : entry.mid(entry.indexOf(QLatin1Char(':')) + 1).split(QLatin1Char(','), Qt::SkipEmptyParts))
                s.insert(ext.toLower());
        return s;
    }();
    return set;
}

const QSet<QString> &qtFormats()
{
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            s.insert(QString::fromLatin1(format).toLower());
        return s;
    }();
    return set;
}

} // namespace

QStringList Format::suffixes() const
{
    return QString::fromLatin1(extensions).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

const QList<Format> &formats()
{
    return kFormats;
}

bool isAvailable(const Format &format)
{
    const QString first = format.suffixes().constFirst();
    switch (format.decoder) {
    case Decoder::Jxl:
#ifdef IMAGEVIEWER_HAVE_LIBJXL
        return true;
#else
        return oiioSuffixes().contains(first);
#endif
    case Decoder::WebP:
#ifdef IMAGEVIEWER_HAVE_LIBWEBP
        return true;
#else
        return oiioSuffixes().contains(first);
#endif
    case Decoder::Apng: return oiioSuffixes().contains(QStringLiteral("png")); // frames go through its PNG reader
    case Decoder::Heif: return heifSequencesAvailable(); // without it, the "avif" row reads the still image
    case Decoder::OpenImageIO: return oiioSuffixes().contains(first);
    case Decoder::Qt: return qtFormats().contains(first);
    case Decoder::System: return systemHeicAvailable(); // HEIC is the only one so far
    case Decoder::GraphicsMagick: return graphicsMagickAvailable();
    }
    return false;
}

const Format *detectFormat(QByteArrayView head, const QString &suffix)
{
    const QString lower = suffix.toLower();
    const Format *bySuffix = nullptr;
    for (const Format &f : kFormats)
        if (!bySuffix && f.suffixes().contains(lower))
            bySuffix = &f;
    if (bySuffix && bySuffix->signature && bySuffix->signature(head))
        return bySuffix; // content and name agree
    const Format *byContent = nullptr;
    for (const Format &f : kFormats) {
        if (f.signature && f.signature(head)) {
            byContent = &f;
            break;
        }
    }
    if (!byContent)
        return bySuffix; // unrecognised content: the name is all there is (TGA, RAW) or nothing
    if (byContent->container && bySuffix && !bySuffix->signature)
        return bySuffix; // a TIFF-based camera RAW file
    return byContent;    // the content wins over a misleading name
}

const QStringList &supportedSuffixes()
{
    static const QStringList list = [] {
        QSet<QString> set;
        for (const Format &f : kFormats)
            if (isAvailable(f) || f.decoder == Decoder::System)
                for (const QString &s : f.suffixes())
                    set.insert(s);
        QStringList sorted(set.begin(), set.end());
        sorted.sort();
        return sorted;
    }();
    return list;
}

QStringList formatReport()
{
    // id | name | decoder | availability | capabilities | extensions | test file (tests/smoke.sh reads it)
    QStringList lines;
    for (const Format &f : kFormats) {
        QStringList caps;
        if (f.capabilities & CanHdr)
            caps << QStringLiteral("hdr");
        if (f.capabilities & CanAlpha)
            caps << QStringLiteral("alpha");
        if (f.capabilities & CanAnimate)
            caps << QStringLiteral("animation");
        if (f.capabilities & CanHavePages)
            caps << QStringLiteral("pages");
        lines << QStringList{QString::fromLatin1(f.id), QString::fromLatin1(f.name),
                             QString::fromLatin1(decoderName(f.decoder)),
                             isAvailable(f)                   ? QStringLiteral("yes")
                             : f.decoder == Decoder::System ? QStringLiteral("system-missing")
                                                            : QStringLiteral("no"),
                             caps.join(QLatin1Char(',')),
                             f.suffixes().join(QLatin1Char(' ')), QString::fromLatin1(f.testFile ? f.testFile : "")}
                     .join(QStringLiteral(" | "));
    }
    return lines;
}
