// What every decoder back end shares (limits, messages, colour helpers), and the choice among them
// for a file (decodeFile(), D-38). The OpenImageIO back end is oiio.cpp.
#include "decoders.h"

#include <OpenImageIO/oiioversion.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSysInfo>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using color::Descriptor;
using color::Transfer;

constexpr qint64 kWorkingBytesPerPixel = 16;    // RGBA16F result plus one orient/downscale copy

bool decodeWith(Decoder decoder, const QString &path, const Format *format, qint64 maxPixels, Decoded *out,
                QString *error, std::unique_ptr<FrameReader> *frames, DecodeOptions options)
{
    switch (decoder) {
    case Decoder::Jxl:
    case Decoder::WebP:
    case Decoder::Apng:
    case Decoder::Softimage: {
        if (!codecAvailable(decoder))
            break;
        QByteArray bytes;
        if (!readWholeFile(path, &bytes, error))
            return false;
        return decodeCodec(decoder, std::move(bytes), maxPixels, out, error, frames);
    }
    case Decoder::Heif: {
        QByteArray bytes;
        if (!readWholeFile(path, &bytes, error))
            return false;
        return decodeHeifSequence(std::move(bytes), maxPixels, out, error, frames);
    }
    case Decoder::Qt: return decodeWithQt(path, format, maxPixels, out, error);
    case Decoder::System: return decodeSystemHeic(path, maxPixels, out, error);
    case Decoder::GraphicsMagick:
    case Decoder::Svg:
        if (!format)
            break;
        return decodeInWorker(*format, path, maxPixels, out, error, options);
    case Decoder::OpenImageIO: break;
    }
    if (options.preview && format && std::strcmp(format->id, "raw") == 0) {
        if (decodeRawPreview(path, options.maxSide, maxPixels, out))
            return true;
        if (out->overLimit) // its preview over the limit: the raw data is larger still
            return false;
        *out = Decoded();
    }
    // A HEIF or AVIF still: read once, for OpenImageIO and for its colour, which libheif gives
    // (describeHeifStill), where this OpenImageIO reads HEIF from memory.
    if (format && (std::strcmp(format->id, "avif") == 0 || std::strcmp(format->id, "heic") == 0)
        && format->decoder != Decoder::Heif && oiioReadsHeifFromMemory()) {
        QByteArray bytes;
        if (!readWholeFile(path, &bytes, error))
            return false;
        if (!decodeOiioMemory(bytes, ("image." + format->suffixes().constFirst().toStdString()).c_str(), out, error, maxPixels))
            return false;
        describeHeifStill(bytes, &out->colour);
        return true;
    }
    // GIF animations are OpenImageIO's subimages; pages of other formats are not frames.
    const bool animatable = frames && format && format->decoder == Decoder::OpenImageIO && (format->capabilities & CanAnimate);
    if (!decodeWithOiio(path, format, maxPixels, out, error, animatable ? frames : nullptr))
        return false;
    if (out->codec == QLatin1String("OpenImageIO/heif")) {
        QByteArray bytes;
        QString ignored;
        if (readWholeFile(path, &bytes, &ignored))
            describeHeifStill(bytes, &out->colour);
    }
    return true;
}

} // namespace

// Refuses images whose decode would need more than ~60 % of physical memory:
// failing early with a message beats swapping the machine or an OOM kill.
qint64 decodeMemoryBudget()
{
    static const qint64 budget = physicalMemoryBytes() / 10 * 6;
    return budget;
}

bool fitsInMemory(qint64 pixels, int nativeBytesPerPixel, QString *error)
{
    const qint64 budget = decodeMemoryBudget();
    const qint64 needed = pixels * (nativeBytesPerPixel + kWorkingBytesPerPixel);
    if (budget <= 0 || needed <= budget)
        return true;
    //: GB: gigabytes.
    *error = QCoreApplication::translate("Image", "image too large for the available memory (needs %1 GB, limit %2 GB)")
                 .arg(QLocale().toString(double(needed) / (1 << 30), 'f', 1), QLocale().toString(double(budget) / (1 << 30), 'f', 1));
    return false;
}

bool checkDimensions(qint64 width, qint64 height, int channels, int nativeBytesPerPixel, qint64 maxPixels, Decoded *out,
                     QString *error, qint64 depth)
{
    // Each factor is bounded before it is multiplied: two 32-bit dimensions would overflow.
    if (width <= 0 || height <= 0 || depth <= 0 || channels <= 0 || width > kMaxPixels || height > kMaxPixels
        || depth > kMaxPixels || width * height > kMaxPixels || width * height * depth > kMaxPixels) {
        *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(width).arg(height).arg(channels);
        return false;
    }
    if (maxPixels > 0 && width * height > maxPixels) {
        out->overLimit = true;
        return false;
    }
    return fitsInMemory(width * height * depth, nativeBytesPerPixel, error);
}

QString libraryError(const char *library, const std::string &message, const std::string &format)
{
    qCInfo(lcDecode).noquote() << library << QString::fromStdString(message).trimmed();
    return format.empty() ? QString() : damaged(QString::fromStdString(format).toUpper().toUtf8().constData());
}

// A file that names its format but cannot be read as one (one message for every back end).
QString damaged(const char *formatName)
{
    return QCoreApplication::translate("Image", "damaged, truncated or unsupported %1 file").arg(QString::fromLatin1(formatName));
}

QString cleanText(QString text)
{
    text.truncate(256);
    if (!text.isEmpty() && text.back().isHighSurrogate()) // never half a character
        text.chop(1);
    text = text.simplified();
    text.removeIf([](QChar c) { return c.category() == QChar::Other_Control || c.category() == QChar::Other_Format; });
    text = text.simplified(); // tabs and newlines became spaces first; removed characters may leave two
    if (text.size() > 64) {
        text.truncate(63);
        if (text.back().isHighSurrogate()) // never half a character
            text.chop(1);
        text += QChar(0x2026);
    }
    return text;
}

void describeIcc(const QByteArray &icc, Descriptor *d)
{
    d->source = Descriptor::Source::Icc;
    d->icc = icc;
    const QString name = cleanText(color::iccDescription(icc)); // the profile's own text
    d->description = QStringLiteral("ICC: %1").arg(name.isEmpty() ? QCoreApplication::translate("Image", "(no description)") : name);
}

QString cmykWithoutProfile()
{
    return QCoreApplication::translate("Image", "CMYK, converted without color management");
}

void assumeDefault(bool isFloat, Descriptor *d)
{
    d->source = Descriptor::Source::Assumed;
    d->primaries = color::kBt709;
    d->transfer = isFloat ? Transfer::Linear : Transfer::Srgb;
    d->description = isFloat ? QCoreApplication::translate("Image", "linear BT.709 (assumed)") : QCoreApplication::translate("Image", "sRGB (assumed)");
}

// How long a frame is shown: browsers play delays of 10 ms or less at 100 ms (GIF and WebP
// files rely on it), and nothing waits longer than a minute.
int playableMs(double ms)
{
    return ms <= 10.0 ? 100 : int(std::lround(std::min(ms, 60000.0)));
}

// The first frame's layout (size, samples, channels, colour) on a later frame of the same file.
void copyLayout(const Decoded &from, Decoded *to)
{
    to->width = from.width;
    to->height = from.height;
    to->sample = from.sample;
    to->channels = from.channels;
    to->sourceChannels = from.sourceChannels;
    to->alphaIndex = from.alphaIndex;
    to->associatedAlpha = from.associatedAlpha;
    to->gray = from.gray;
    to->cmyk = from.cmyk;
    to->inkInverted = from.inkInverted;
    to->bits = from.bits;
    to->orientation = from.orientation;
    to->colour = from.colour;
    to->codec = from.codec;
}

std::size_t frameBytes(const Decoded &d)
{
    return std::size_t(d.width) * std::size_t(d.height) * std::size_t(d.channels) * std::size_t(d.sampleBytes());
}

// The whole file in memory, for decoders that parse from a buffer. Read, not mapped: a mapping
// outlives the decode in an animation, and another program truncating the file would then
// crash the viewer (SIGBUS).
bool readWholeFile(const QString &path, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    if (file.size() > kMaxFileBytes) { // a fixed limit, whatever the memory
        //: GB: gigabytes. %1: the file's size; %2: the largest file this format is read from.
        *error = QCoreApplication::translate("Image", "file too large to read (%1 GB; at most %2 GB)")
                     .arg(QLocale().toString(double(file.size()) / (1 << 30), 'f', 1),
                          QLocale().toString(double(kMaxFileBytes) / (1 << 30), 'f', 1));
        return false;
    }
    *bytes = file.readAll();
    if (bytes->size() != file.size()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool decodeFile(const QString &path, qint64 maxPixels, Decoded *out, QString *error, std::unique_ptr<FrameReader> *frames,
                DecodeOptions options)
{
    stopIdleSvgWorker();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    // Detection by content (D-38): the format's own back end first, then OpenImageIO (Qt reads
    // only its own formats, D-67).
    const QByteArray head = file.read(kFormatHeadBytes);
    file.close();
    const Format *format = detectFormat(head, QFileInfo(path).suffix());
    QList<Decoder> order;
    if (format && isAvailable(*format))
        order << format->decoder;
    // Softimage PIC content under another format's name: our reader, as OpenImageIO's must not see it (D-45).
    for (const Format &f : formats())
        if (f.decoder == Decoder::Softimage && f.signature && f.signature(head) && !order.contains(f.decoder))
            order << f.decoder;
    // A format whose OpenImageIO reader crashes on damaged files never reaches it (D-45), not even
    // under the name of another format.
    const bool notOiio = (format && !oiioMayRead(*format)) || !oiioMayReadContent(head);
    if (!order.contains(Decoder::OpenImageIO) && !notOiio)
        order << Decoder::OpenImageIO;
    QString firstError;
    for (Decoder decoder : std::as_const(order)) {
        *out = Decoded(); // nothing from a failed attempt may leak into the next one
        if (frames)
            frames->reset();
        QString why;
        if (decodeWith(decoder, path, format, maxPixels, out, &why, frames, options))
            return true;
        if (out->overLimit)
            return false;
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) {
            *error = QStringLiteral("cancelled"); // never shown: the caller asked for it
            return false;
        }
        if (firstError.isEmpty())
            firstError = why;
    }
    if (frames)
        frames->reset();
    // A HEIC file where the system has no HEVC decoder (and OpenImageIO's libheif has none
    // either): say how to get one rather than why the last decoder failed.
    const bool noSystemCodec = format && format->decoder == Decoder::System && !isAvailable(*format);
    if (noSystemCodec)
        *error = missingHevcMessage();
    else if (!firstError.isEmpty())
        *error = firstError;
    else if (format) // only libraries' reasons, which stay in the log: the format by its name
        *error = damaged(format->name);
    else if (const QString suffix = QFileInfo(path).suffix().toUpper(); !suffix.isEmpty())
        *error = damaged(suffix.toUtf8().constData()); // .xcf: "unsupported XCF file"
    else
        *error = QCoreApplication::translate("Image", "damaged, truncated or unsupported file");
    return false;
}

QString decoderName(Decoder decoder)
{
    switch (decoder) {
    case Decoder::OpenImageIO:
    case Decoder::Apng: return QStringLiteral("OpenImageIO"); // APNG frames are PNGs that OpenImageIO decodes
    case Decoder::Jxl: return QStringLiteral("libjxl");
    case Decoder::WebP: return QStringLiteral("libwebp");
    case Decoder::Heif: return QStringLiteral("libheif");
    case Decoder::Qt: return QStringLiteral("Qt");
    case Decoder::GraphicsMagick: return QStringLiteral("GraphicsMagick");
    case Decoder::Svg: return svgRenderer().section(QLatin1Char(' '), 0, 0); // "resvg"
    case Decoder::Softimage: return QStringLiteral("ImageViewer"); // our own reader
    case Decoder::System:
#if defined(Q_OS_MACOS)
        return QStringLiteral("ImageIO");
#elif defined(Q_OS_WIN)
        return QStringLiteral("WIC");
#else
        break;
#endif
    }
    return QStringLiteral("system");
}

QString decoderVersion(Decoder decoder)
{
    switch (decoder) {
    case Decoder::OpenImageIO:
    case Decoder::Apng: return QString::fromLatin1(OIIO_VERSION_STRING);
    case Decoder::Jxl: return jxlVersion();
    case Decoder::WebP: return webpVersion();
    case Decoder::Heif: return heifVersion();
    case Decoder::Qt: return QString::fromLatin1(qVersion());
    case Decoder::GraphicsMagick: return graphicsMagickVersion();
    case Decoder::Svg: return svgRenderer().section(QLatin1Char(' '), 1); // "0.48.1"
    case Decoder::Softimage: return QCoreApplication::applicationVersion();
    case Decoder::System:
#if defined(Q_OS_MACOS)
        return QStringLiteral("macOS %1").arg(QSysInfo::productVersion());
#elif defined(Q_OS_WIN)
        return QStringLiteral("Windows %1").arg(QSysInfo::productVersion());
#else
        break;
#endif
    }
    return {};
}
