#include "decoders.h"

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/tiffutils.h>

#include <QColorSpace>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef IMAGEVIEWER_HAVE_LIBJXL
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>
#include <jxl/version.h>
#endif
#ifdef IMAGEVIEWER_HAVE_LIBWEBP
#include <webp/demux.h>
#endif

namespace {

using color::Descriptor;
using color::Transfer;

constexpr qint64 kWorkingBytesPerPixel = 16;    // RGBA16F result plus one orient/downscale copy

// Refuses images whose decode would need more than ~60 % of physical memory:
// failing early with a message beats swapping the machine or an OOM kill.
bool fitsInMemory(qint64 pixels, int nativeBytesPerPixel, QString *error)
{
    static const qint64 budget = physicalMemoryBytes() / 10 * 6;
    const qint64 needed = pixels * (nativeBytesPerPixel + kWorkingBytesPerPixel);
    if (budget <= 0 || needed <= budget)
        return true;
    *error = QCoreApplication::translate("Image", "image too large for the available memory (needs %1 GB, limit %2 GB)")
                 .arg(double(needed) / (1 << 30), 0, 'f', 1)
                 .arg(double(budget) / (1 << 30), 0, 'f', 1);
    return false;
}

// Parses an OpenImageIO colour space name. OIIO 3 uses colour interop ids of the
// form <transfer>_<primaries>_<scene|display>; OIIO 2 used ad-hoc names.
bool parseOiioColorSpace(const QString &name, Descriptor *d)
{
    const QString n = name.toLower();
    if (n.isEmpty())
        return false;
    const QStringList parts = n.split(QLatin1Char('_'));
    if (parts.size() >= 2) {
        static const QHash<QString, Transfer> transfers = {
            {"lin", Transfer::Linear}, {"srgb", Transfer::Srgb}, {"srgbe", Transfer::Srgb},
            {"g24", Transfer::Bt1886}, {"pq", Transfer::Pq}, {"hlg", Transfer::Hlg}};
        static const QHash<QString, color::Chromaticities> primaries = {
            {"rec709", color::kBt709}, {"srgb", color::kBt709}, {"p3d65", color::kDisplayP3},
            {"rec2020", color::kBt2020}, {"adobergb", color::kAdobeRgb}, {"ap0", color::kAcesAp0},
            {"ap1", color::kAcesAp1}};
        static const QRegularExpression powerId(QStringLiteral("^g(\\d{2})$")); // g18, g22, g26: gamma x 10
        const QRegularExpressionMatch power = powerId.match(parts[0]);
        if (primaries.contains(parts[1]) && (transfers.contains(parts[0]) || power.hasMatch())) {
            d->primaries = primaries.value(parts[1]);
            if (transfers.contains(parts[0])) {
                d->transfer = transfers.value(parts[0]);
            } else {
                d->transfer = Transfer::Power;
                d->gamma = power.captured(1).toFloat() / 10.0f;
            }
            return true;
        }
    }
    d->primaries = color::kBt709;
    if (n == "linear" || n == "scene_linear" || n == "lin_srgb" || n == "lin_rec709") {
        d->transfer = Transfer::Linear;
        return true;
    }
    if (n == "srgb") {
        d->transfer = Transfer::Srgb;
        return true;
    }
    if (n == "rec709") {
        d->transfer = Transfer::Bt1886;
        return true;
    }
    if (n == "acescg") {
        d->transfer = Transfer::Linear;
        d->primaries = color::kAcesAp1;
        return true;
    }
    if (n == "aces2065-1" || n == "aces") {
        d->transfer = Transfer::Linear;
        d->primaries = color::kAcesAp0;
        return true;
    }
    // OIIO 2.4 reports PNG gAMA and similar as "Gamma2.2" / "GammaCorrected2.2".
    static const QRegularExpression gammaName(QStringLiteral("^gamma(?:corrected)?(\\d+(?:\\.\\d+)?)$"));
    if (const QRegularExpressionMatch m = gammaName.match(n); m.hasMatch()) {
        const float g = m.captured(1).toFloat();
        if (g > 0.5f && g < 5.0f) {
            d->transfer = Transfer::Power;
            d->gamma = g;
            return true;
        }
    }
    return false;
}

// Fills `d` from the metadata OpenImageIO attached to the image.
void describeOiio(const OIIO::ImageSpec &spec, bool isFloat, const char *format, Descriptor *d)
{
    // CICP first: HDR JPEG XL and PNG (3rd edition) carry it next to an ICC profile,
    // which for PQ/HLG is only an SDR approximation; the PNG specification gives cICP
    // precedence over iCCP.
    if (const OIIO::ParamValue *p = spec.find_attribute("CICP");
        p && p->type() == OIIO::TypeDesc(OIIO::TypeDesc::INT, 4)) {
        const int *v = static_cast<const int *>(p->data());
        Descriptor c;
        if (color::primariesFromCicp(v[0], &c.primaries) && color::transferFromCicp(v[1], &c.transfer, &c.gamma)) {
            d->source = Descriptor::Source::Cicp;
            d->primaries = c.primaries;
            d->transfer = c.transfer;
            d->gamma = c.gamma;
            d->fullRange = v[3] != 0;
            d->description = QStringLiteral("CICP %1/%2/%3/%4 — %5")
                                 .arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3])
                                 .arg(color::transferName(c.transfer, c.gamma));
            return;
        }
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("ICCProfile");
        p && p->type().basetype == OIIO::TypeDesc::UINT8 && p->type().size() > 0) {
        d->source = Descriptor::Source::Icc;
        d->icc = QByteArray(static_cast<const char *>(p->data()), qsizetype(p->type().size()));
        const QString name = color::iccDescription(d->icc);
        d->description = QStringLiteral("ICC: %1").arg(name.isEmpty() ? QCoreApplication::translate("Image", "(no description)") : name);
        return;
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("chromaticities");
        p && p->type() == OIIO::TypeDesc(OIIO::TypeDesc::FLOAT, 8)) {
        const float *c = static_cast<const float *>(p->data());
        const color::Chromaticities chroma = {{c[0], c[1]}, {c[2], c[3]}, {c[4], c[5]}, {c[6], c[7]}};
        if (color::isUsable(chroma)) {
            d->source = Descriptor::Source::FormatAttributes;
            d->primaries = chroma;
            d->transfer = Transfer::Linear;
            d->description = QCoreApplication::translate("Image", "linear, chromaticities from the file");
            return;
        }
        d->description = QCoreApplication::translate("Image", "linear BT.709 (assumed: the file's chromaticities are invalid)");
        d->source = Descriptor::Source::Assumed;
        d->primaries = color::kBt709;
        d->transfer = Transfer::Linear;
        return;
    }
    // PFM carries no colour metadata and is linear by convention (HDR radiance maps), but
    // OIIO labels every PNM variant "Rec709"; decoding that as BT.1886 turned 36.0 into 5434.
    const bool floatPnm = isFloat && std::strcmp(format, "pnm") == 0;
    const QString cs = floatPnm ? QString() : QString::fromStdString(spec.get_string_attribute("oiio:ColorSpace"));
    if (parseOiioColorSpace(cs, d)) {
        // OIIO also fills this in when the file carries no colour tag at all, so it
        // is reported as the decoder's interpretation, not as file metadata (F12).
        d->source = Descriptor::Source::FormatAttributes;
        d->description = QCoreApplication::translate("Image", "%1 (assigned by the decoder)").arg(cs);
        return;
    }
    d->source = Descriptor::Source::Assumed;
    d->primaries = color::kBt709;
    d->transfer = isFloat ? Transfer::Linear : Transfer::Srgb;
    d->description = isFloat ? QCoreApplication::translate("Image", "linear BT.709 (assumed)") : QCoreApplication::translate("Image", "sRGB (assumed)");
}

// EXIF text is file content: no control characters (they would break the panel's layout),
// one line, bounded length.
QString cleanText(const std::string &raw)
{
    QString text = QString::fromUtf8(raw.data(), qsizetype(std::min<std::size_t>(raw.size(), 256))).simplified();
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

// Shooting data as OpenImageIO names it (EXIF tags, also filled by the RAW reader).
CameraInfo cameraFromOiio(const OIIO::ImageSpec &spec)
{
    CameraInfo camera;
    camera.make = cleanText(spec.get_string_attribute("Make"));
    camera.model = cleanText(spec.get_string_attribute("Model"));
    camera.lens = cleanText(spec.get_string_attribute("Exif:LensModel"));
    const QString taken = cleanText(spec.get_string_attribute("Exif:DateTimeOriginal"));
    camera.taken = QDateTime::fromString(taken.left(19), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    // Values outside any real camera's range are treated as absent rather than shown.
    const auto inRange = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi ? v : 0.0f; };
    camera.exposureTime = inRange(spec.get_float_attribute("ExposureTime"), 1e-6f, 1e5f);
    camera.fNumber = inRange(spec.get_float_attribute("FNumber"), 0.5f, 1000.0f);
    camera.focalLength = inRange(spec.get_float_attribute("Exif:FocalLength"), 0.1f, 1e5f);
    int iso = spec.get_int_attribute("Exif:PhotographicSensitivity");
    if (iso <= 0)
        iso = spec.get_int_attribute("Exif:ISOSpeedRatings");
    camera.iso = iso > 0 && iso <= 10'000'000 ? iso : 0;
    return camera;
}

bool decodeWithOiio(const QString &path, qint64 maxPixels, Decoded *out, QString *error)
{
    OIIO::ImageSpec config;
    // Straight alpha where the format stores it (PNG, TIFF, WebP, HEIF...): OIIO would
    // otherwise premultiply the encoded values, which is wrong for non-linear data.
    config.attribute("oiio:UnassociatedAlpha", 1);
    // PFM stores rows bottom to top; OIIO 3.1 flips them only when asked (pnminput.cpp).
    config.attribute("pnm:pfmflip", 1);
    auto in = OIIO::ImageInput::open(path.toUtf8().toStdString(), &config);
    if (!in) {
        *error = QString::fromStdString(OIIO::geterror());
        return false;
    }
    const OIIO::ImageSpec &spec = in->spec();
    const int w = spec.width, h = spec.height, nch = spec.nchannels;
    if (w <= 0 || h <= 0 || nch <= 0 || qint64(w) * h > kMaxPixels) {
        *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(w).arg(h).arg(nch);
        return false;
    }
    if (maxPixels > 0 && qint64(w) * h > maxPixels) {
        out->overLimit = true;
        return false;
    }

    using Sample = Decoded::Sample;
    const OIIO::TypeDesc stored = spec.format;
    switch (stored.basetype) {
    case OIIO::TypeDesc::UINT8: out->sample = Sample::U8; break;
    case OIIO::TypeDesc::INT8:
    case OIIO::TypeDesc::UINT16:
    case OIIO::TypeDesc::INT16:
    case OIIO::TypeDesc::UINT32: // integer semantics (and transfer); 16 bits are plenty for display
    case OIIO::TypeDesc::INT32: out->sample = Sample::U16; break;
    case OIIO::TypeDesc::HALF: out->sample = Sample::F16; break;
    default: out->sample = Sample::F32; break;
    }
    const OIIO::TypeDesc request = out->sample == Sample::U8    ? OIIO::TypeDesc::UINT8
                                   : out->sample == Sample::U16 ? OIIO::TypeDesc::UINT16
                                   : out->sample == Sample::F16 ? OIIO::TypeDesc::HALF
                                                                : OIIO::TypeDesc::FLOAT;
    // Only colour and alpha are read: an EXR with 40 AOV channels must not cost 10x the memory.
    const int alpha = spec.alpha_channel >= 0 && spec.alpha_channel < nch ? spec.alpha_channel : -1;
    const bool gray = nch < 3;
    const int readChannels = std::max(gray ? 1 : 3, alpha + 1);
    const qint64 pixels = qint64(w) * h;
    if (!fitsInMemory(pixels, readChannels * out->sampleBytes(), error))
        return false;
    out->data.reset(new unsigned char[std::size_t(pixels) * std::size_t(readChannels * out->sampleBytes())]);
    if (!in->read_image(0, 0, 0, readChannels, request, out->data.get())) {
        *error = QString::fromStdString(in->geterror());
        return false;
    }

    out->width = w;
    out->height = h;
    out->channels = readChannels;
    out->sourceChannels = nch;
    out->alphaIndex = alpha;
    out->associatedAlpha = alpha >= 0 && spec.get_int_attribute("oiio:UnassociatedAlpha", 0) == 0;
    out->gray = gray;
    out->bits = spec.get_int_attribute("oiio:BitsPerSample", int(stored.size() * 8));
    out->orientation = spec.get_int_attribute("Orientation", 1);
    out->codec = QStringLiteral("OpenImageIO/%1").arg(QString::fromUtf8(in->format_name()));
    out->camera = cameraFromOiio(spec);
    describeOiio(spec, !out->isInteger(), in->format_name(), &out->colour);
    return true;
}

// Describes a QColorSpace that was not built from an ICC profile (PNG gAMA/cHRM/sRGB chunks).
bool describeQtColorSpace(const QColorSpace &cs, Descriptor *d)
{
    switch (cs.primaries()) {
    case QColorSpace::Primaries::SRgb: d->primaries = color::kBt709; break;
    case QColorSpace::Primaries::AdobeRgb: d->primaries = color::kAdobeRgb; break;
    case QColorSpace::Primaries::DciP3D65: d->primaries = color::kDisplayP3; break;
    case QColorSpace::Primaries::Bt2020: d->primaries = color::kBt2020; break;
    default: return false;
    }
    switch (cs.transferFunction()) {
    case QColorSpace::TransferFunction::Linear: d->transfer = Transfer::Linear; break;
    case QColorSpace::TransferFunction::SRgb: d->transfer = Transfer::Srgb; break;
    case QColorSpace::TransferFunction::Gamma:
        d->transfer = Transfer::Power;
        d->gamma = cs.gamma();
        break;
    case QColorSpace::TransferFunction::Bt2020: d->transfer = Transfer::Bt1886; break; // decision D-12
    case QColorSpace::TransferFunction::St2084: d->transfer = Transfer::Pq; break;
    case QColorSpace::TransferFunction::Hlg: d->transfer = Transfer::Hlg; break;
    default: return false;
    }
    d->source = Descriptor::Source::FormatAttributes;
    d->description = QCoreApplication::translate("Image", "%1 (file metadata, via Qt)").arg(cs.description());
    return true;
}

bool decodeWithQt(const QString &path, qint64 maxPixels, Decoded *out, QString *error)
{
    QImageReader reader(path);
    // Qt's SVG reader lays out text with QFontDatabase, which aborts without a QGuiApplication
    // (console modes such as --info run on a QCoreApplication).
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())
        && (reader.format() == "svg" || reader.format() == "svgz")) {
        *error = QCoreApplication::translate("Image", "SVG is only decoded in the graphical interface");
        return false;
    }
    if (const QSize size = reader.size(); maxPixels > 0 && size.isValid() && qint64(size.width()) * size.height() > maxPixels) {
        out->overLimit = true;
        return false;
    }
    reader.setAutoTransform(true); // orientation is applied by Qt
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return false;
    }
    if (!fitsInMemory(qint64(image.width()) * image.height(), 16, error))
        return false;
    const QColorSpace cs = image.colorSpace();
    // Keep the source precision: float formats stay float (values above 1 survive),
    // anything deeper than 8 bits per channel goes through 16 bits.
    const QPixelFormat pf = image.pixelFormat();
    const bool isFloat = pf.typeInterpretation() == QPixelFormat::FloatingPoint;
    const bool deep = !isFloat && (pf.redSize() > 8 || image.depth() / std::max<uint>(pf.channelCount(), 1) > 8);
    out->width = image.width();
    out->height = image.height();
    out->sample = isFloat ? Decoded::Sample::F32 : deep ? Decoded::Sample::U16 : Decoded::Sample::U8;
    out->channels = 4;
    out->sourceChannels = int(pf.channelCount());
    out->alphaIndex = image.hasAlphaChannel() ? 3 : -1;
    out->gray = false;
    out->bits = isFloat ? 32 : deep ? 16 : 8;
    out->orientation = 1;
    out->codec = QStringLiteral("Qt/%1").arg(QString::fromLatin1(reader.format()));
    if (cs.isValid() && !cs.iccProfile().isEmpty()) {
        out->colour.source = Descriptor::Source::Icc;
        out->colour.icc = cs.iccProfile();
        out->colour.description = QStringLiteral("ICC: %1").arg(cs.description());
    } else if (!(cs.isValid() && describeQtColorSpace(cs, &out->colour))) {
        out->colour.transfer = isFloat ? Transfer::Linear : Transfer::Srgb;
        out->colour.description = isFloat ? QCoreApplication::translate("Image", "linear BT.709 (assumed)") : QCoreApplication::translate("Image", "sRGB (assumed)");
    }
    image.setColorSpace(QColorSpace()); // keep the encoded values untouched
    image.convertTo(isFloat ? QImage::Format_RGBA32FPx4 : deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    const std::size_t rowBytes = std::size_t(out->width) * 4 * std::size_t(out->sampleBytes());
    out->data.reset(new unsigned char[rowBytes * std::size_t(out->height)]);
    for (int y = 0; y < out->height; ++y)
        std::memcpy(out->data.get() + rowBytes * std::size_t(y), image.constScanLine(y), rowBytes);
    return true;
}


// A file that names its format but cannot be read as one (one message for every back end).
QString damaged(const char *formatName)
{
    return QCoreApplication::translate("Image", "damaged, truncated or unsupported %1 file").arg(QString::fromLatin1(formatName));
}

// Shooting data and orientation from an EXIF block (TIFF header first), through OpenImageIO.
CameraInfo cameraFromExif(QByteArrayView tiff, int *orientation)
{
    OIIO::ImageSpec spec;
    if (tiff.size() < 8
        || !OIIO::decode_exif(OIIO::cspan<uint8_t>(reinterpret_cast<const uint8_t *>(tiff.data()), tiff.size()), spec))
        return {};
    if (orientation) {
        const int o = spec.get_int_attribute("Orientation", 1);
        *orientation = o >= 1 && o <= 8 ? o : 1;
    }
    return cameraFromOiio(spec);
}

void describeIcc(const QByteArray &icc, Descriptor *d)
{
    d->source = Descriptor::Source::Icc;
    d->icc = icc;
    const QString name = color::iccDescription(icc);
    d->description = QStringLiteral("ICC: %1").arg(name.isEmpty() ? QCoreApplication::translate("Image", "(no description)") : name);
}

void assumeDefault(bool isFloat, Descriptor *d)
{
    d->source = Descriptor::Source::Assumed;
    d->primaries = color::kBt709;
    d->transfer = isFloat ? Transfer::Linear : Transfer::Srgb;
    d->description = isFloat ? QCoreApplication::translate("Image", "linear BT.709 (assumed)")
                             : QCoreApplication::translate("Image", "sRGB (assumed)");
}

#ifdef IMAGEVIEWER_HAVE_LIBJXL
// libjxl 0.9 dropped the unused pixel-format argument of the colour queries.
#if JPEGXL_NUMERIC_VERSION >= JPEGXL_COMPUTE_NUMERIC_VERSION(0, 9, 0)
#define JXL_COLOR_ARGS(dec) (dec), JXL_COLOR_PROFILE_TARGET_DATA
#else
#define JXL_COLOR_ARGS(dec) (dec), nullptr, JXL_COLOR_PROFILE_TARGET_DATA
#endif

// JPEG XL's enumerated colour encoding (the CICP-like description of D-22).
bool describeJxl(const JxlColorEncoding &e, Descriptor *d)
{
    if (e.color_space != JXL_COLOR_SPACE_RGB && e.color_space != JXL_COLOR_SPACE_GRAY)
        return false;
    color::Chromaticities c = color::kBt709;
    QString primaries = QStringLiteral("BT.709");
    if (e.color_space == JXL_COLOR_SPACE_RGB) {
        switch (e.primaries) {
        case JXL_PRIMARIES_SRGB: break;
        case JXL_PRIMARIES_2100: c = color::kBt2020; primaries = QStringLiteral("BT.2020"); break;
        case JXL_PRIMARIES_P3: c = color::kDisplayP3; primaries = QStringLiteral("P3"); break;
        case JXL_PRIMARIES_CUSTOM:
            c = {{e.primaries_red_xy[0], e.primaries_red_xy[1]}, {e.primaries_green_xy[0], e.primaries_green_xy[1]},
                 {e.primaries_blue_xy[0], e.primaries_blue_xy[1]}, {0.3127, 0.3290}};
            primaries = QCoreApplication::translate("Image", "custom primaries");
            break;
        default: return false;
        }
    }
    switch (e.white_point) {
    case JXL_WHITE_POINT_D65: c.w[0] = 0.3127; c.w[1] = 0.3290; break;
    case JXL_WHITE_POINT_E: c.w[0] = c.w[1] = 1.0 / 3.0; break;
    case JXL_WHITE_POINT_DCI: c.w[0] = 0.314; c.w[1] = 0.351; break;
    case JXL_WHITE_POINT_CUSTOM: c.w[0] = e.white_point_xy[0]; c.w[1] = e.white_point_xy[1]; break;
    default: return false;
    }
    if (!color::isUsable(c))
        return false;
    Transfer transfer = Transfer::Srgb;
    float gamma = 2.2f;
    switch (e.transfer_function) {
    case JXL_TRANSFER_FUNCTION_709: transfer = Transfer::Bt1886; break; // decision D-12
    case JXL_TRANSFER_FUNCTION_LINEAR: transfer = Transfer::Linear; break;
    case JXL_TRANSFER_FUNCTION_SRGB: transfer = Transfer::Srgb; break;
    case JXL_TRANSFER_FUNCTION_PQ: transfer = Transfer::Pq; break;
    case JXL_TRANSFER_FUNCTION_HLG: transfer = Transfer::Hlg; break;
    case JXL_TRANSFER_FUNCTION_DCI: transfer = Transfer::Power; gamma = 2.6f; break;
    case JXL_TRANSFER_FUNCTION_GAMMA: // stored as the encoding exponent, e.g. 1/2.2
        if (!(e.gamma > 0.1 && e.gamma <= 1.0))
            return false;
        transfer = Transfer::Power;
        gamma = float(1.0 / e.gamma);
        break;
    default: return false;
    }
    d->source = Descriptor::Source::Cicp;
    d->primaries = c;
    d->transfer = transfer;
    d->gamma = gamma;
    d->fullRange = true;
    d->description = QStringLiteral("JPEG XL: %1, %2").arg(primaries, color::transferName(transfer, gamma));
    return true;
}

// The first frame of a JPEG XL file (an animation's first frame, composited), at its
// native depth, with straight alpha and the colour encoding of the decoded samples.
bool decodeJxl(QByteArrayView file, qint64 maxPixels, Decoded *out, QString *error)
{
    const QString invalid = damaged("JPEG XL");
    JxlDecoderPtr dec = JxlDecoderMake(nullptr);
    JxlThreadParallelRunnerPtr runner =
        JxlThreadParallelRunnerMake(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
    if (!dec || !runner
        || JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE | JXL_DEC_BOX)
               != JXL_DEC_SUCCESS
        || JxlDecoderSetParallelRunner(dec.get(), JxlThreadParallelRunner, runner.get()) != JXL_DEC_SUCCESS
        || JxlDecoderSetKeepOrientation(dec.get(), JXL_TRUE) != JXL_DEC_SUCCESS // applied once, by image.cpp
        || JxlDecoderSetUnpremultiplyAlpha(dec.get(), JXL_TRUE) != JXL_DEC_SUCCESS
        || JxlDecoderSetDecompressBoxes(dec.get(), JXL_TRUE) != JXL_DEC_SUCCESS
        || JxlDecoderSetInput(dec.get(), reinterpret_cast<const uint8_t *>(file.data()), std::size_t(file.size()))
               != JXL_DEC_SUCCESS) {
        *error = invalid;
        return false;
    }
    JxlDecoderCloseInput(dec.get());

    JxlBasicInfo info{};
    JxlPixelFormat format{};
    std::vector<uint8_t> exif; // the "Exif" box: 4-byte offset, then a TIFF header
    bool readingExif = false;
    constexpr std::size_t kMaxExifBytes = 1 << 20;
    const auto finishExif = [&] {
        if (!readingExif)
            return;
        exif.resize(exif.size() - JxlDecoderReleaseBoxBuffer(dec.get()));
        readingExif = false;
        if (exif.size() > 4) {
            const std::size_t offset = std::size_t(exif[0]) << 24 | std::size_t(exif[1]) << 16 | std::size_t(exif[2]) << 8 | exif[3];
            if (offset < exif.size() - 4) // the codestream's orientation rules; EXIF's is ignored
                out->camera = cameraFromExif(QByteArrayView(exif.data() + 4 + offset, qsizetype(exif.size() - 4 - offset)), nullptr);
        }
    };
    for (;;) {
        switch (JxlDecoderProcessInput(dec.get())) {
        case JXL_DEC_BASIC_INFO: {
            if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS) {
                *error = invalid;
                return false;
            }
            const qint64 pixels = qint64(info.xsize) * info.ysize;
            if (info.xsize == 0 || info.ysize == 0 || pixels > kMaxPixels) {
                *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)")
                             .arg(info.xsize).arg(info.ysize).arg(info.num_color_channels);
                return false;
            }
            if (maxPixels > 0 && pixels > maxPixels) {
                out->overLimit = true;
                return false;
            }
            using Sample = Decoded::Sample;
            out->sample = info.exponent_bits_per_sample > 0 ? (info.bits_per_sample <= 16 ? Sample::F16 : Sample::F32)
                          : info.bits_per_sample <= 8   ? Sample::U8
                                                        : Sample::U16;
            out->gray = info.num_color_channels == 1;
            const bool alpha = info.alpha_bits > 0;
            out->channels = (out->gray ? 1 : 3) + (alpha ? 1 : 0);
            out->alphaIndex = alpha ? out->channels - 1 : -1;
            out->sourceChannels = int(info.num_color_channels + info.num_extra_channels);
            out->bits = int(info.bits_per_sample);
            out->width = int(info.xsize);
            out->height = int(info.ysize);
            out->orientation = info.orientation >= 1 && info.orientation <= 8 ? int(info.orientation) : 1;
            out->codec = QStringLiteral("libjxl");
            const JxlDataType type = out->sample == Sample::U8    ? JXL_TYPE_UINT8
                                     : out->sample == Sample::U16 ? JXL_TYPE_UINT16
                                     : out->sample == Sample::F16 ? JXL_TYPE_FLOAT16
                                                                  : JXL_TYPE_FLOAT;
            format = {uint32_t(out->channels), type, JXL_NATIVE_ENDIAN, 0};
            if (!fitsInMemory(pixels, out->channels * out->sampleBytes(), error))
                return false;
            break;
        }
        case JXL_DEC_COLOR_ENCODING: {
            // The encoding of the samples the decoder outputs (for lossy XYB files, the one
            // the image was encoded from).
            JxlColorEncoding encoding{};
            if (JxlDecoderGetColorAsEncodedProfile(JXL_COLOR_ARGS(dec.get()), &encoding) == JXL_DEC_SUCCESS
                && describeJxl(encoding, &out->colour))
                break;
            std::size_t size = 0;
            if (JxlDecoderGetICCProfileSize(JXL_COLOR_ARGS(dec.get()), &size) == JXL_DEC_SUCCESS && size > 0
                && size < (std::size_t(1) << 26)) {
                QByteArray icc(qsizetype(size), Qt::Uninitialized);
                if (JxlDecoderGetColorAsICCProfile(JXL_COLOR_ARGS(dec.get()), reinterpret_cast<uint8_t *>(icc.data()), size)
                    == JXL_DEC_SUCCESS) {
                    describeIcc(icc, &out->colour);
                    break;
                }
            }
            assumeDefault(!out->isInteger(), &out->colour);
            break;
        }
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
            std::size_t bytes = 0;
            const std::size_t expected = std::size_t(out->width) * std::size_t(out->height) * std::size_t(out->channels)
                                         * std::size_t(out->sampleBytes());
            if (JxlDecoderImageOutBufferSize(dec.get(), &format, &bytes) != JXL_DEC_SUCCESS || bytes != expected) {
                *error = invalid;
                return false;
            }
            out->data.reset(new unsigned char[bytes]);
            if (JxlDecoderSetImageOutBuffer(dec.get(), &format, out->data.get(), bytes) != JXL_DEC_SUCCESS) {
                *error = invalid;
                return false;
            }
            break;
        }
        case JXL_DEC_BOX: {
            finishExif();
            JxlBoxType type{};
            if (JxlDecoderGetBoxType(dec.get(), type, JXL_TRUE) == JXL_DEC_SUCCESS && std::memcmp(type, "Exif", 4) == 0) {
                exif.assign(4096, 0);
                readingExif = JxlDecoderSetBoxBuffer(dec.get(), exif.data(), exif.size()) == JXL_DEC_SUCCESS;
            }
            break;
        }
        case JXL_DEC_BOX_NEED_MORE_OUTPUT: {
            const std::size_t used = exif.size() - JxlDecoderReleaseBoxBuffer(dec.get());
            if (exif.size() >= kMaxExifBytes) { // not a real EXIF block: stop collecting it
                readingExif = false;
                exif.clear();
                break;
            }
            exif.resize(exif.size() * 2);
            // The new buffer ends where the vector ends, so finishExif()'s arithmetic still holds.
            readingExif = JxlDecoderSetBoxBuffer(dec.get(), exif.data() + used, exif.size() - used) == JXL_DEC_SUCCESS;
            break;
        }
        case JXL_DEC_FULL_IMAGE:
            if (info.have_animation) { // the first frame is enough; trailing boxes are not read
                finishExif();
                return true;
            }
            break; // a still image: read on for EXIF boxes after the codestream
        case JXL_DEC_SUCCESS:
            finishExif();
            if (!out->data) {
                *error = invalid;
                return false;
            }
            return true;
        case JXL_DEC_NEED_MORE_INPUT:
        case JXL_DEC_ERROR:
        default:
            *error = invalid;
            return false;
        }
    }
}
#undef JXL_COLOR_ARGS
#endif // IMAGEVIEWER_HAVE_LIBJXL

#ifdef IMAGEVIEWER_HAVE_LIBWEBP
// The first frame of a WebP file (an animation's first frame, composited), 8-bit straight
// RGBA, with the ICC profile and EXIF the file carries (OpenImageIO assumes sRGB for WebP).
bool decodeWebP(QByteArrayView file, qint64 maxPixels, Decoded *out, QString *error)
{
    const QString invalid = damaged("WebP");
    const WebPData data{reinterpret_cast<const uint8_t *>(file.data()), std::size_t(file.size())};
    std::unique_ptr<WebPDemuxer, void (*)(WebPDemuxer *)> demux(WebPDemux(&data), WebPDemuxDelete);
    if (!demux) {
        *error = invalid;
        return false;
    }
    const int w = int(WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH));
    const int h = int(WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT));
    const uint32_t flags = WebPDemuxGetI(demux.get(), WEBP_FF_FORMAT_FLAGS);
    const qint64 pixels = qint64(w) * h;
    if (w <= 0 || h <= 0 || pixels > kMaxPixels) {
        *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(w).arg(h).arg(4);
        return false;
    }
    if (maxPixels > 0 && pixels > maxPixels) {
        out->overLimit = true;
        return false;
    }
    if (!fitsInMemory(pixels, 4, error))
        return false;
    WebPChunkIterator chunk;
    if ((flags & ICCP_FLAG) && WebPDemuxGetChunk(demux.get(), "ICCP", 1, &chunk)) {
        describeIcc(QByteArray(reinterpret_cast<const char *>(chunk.chunk.bytes), qsizetype(chunk.chunk.size)), &out->colour);
        WebPDemuxReleaseChunkIterator(&chunk);
    } else {
        assumeDefault(false, &out->colour); // the WebP specification's default
    }
    if ((flags & EXIF_FLAG) && WebPDemuxGetChunk(demux.get(), "EXIF", 1, &chunk)) {
        QByteArrayView exif(chunk.chunk.bytes, qsizetype(chunk.chunk.size));
        if (exif.startsWith("Exif\0\0"))
            exif = exif.sliced(6);
        out->camera = cameraFromExif(exif, &out->orientation);
        WebPDemuxReleaseChunkIterator(&chunk);
    }

    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
        *error = invalid;
        return false;
    }
    options.color_mode = MODE_RGBA; // straight alpha
    options.use_threads = 1;
    std::unique_ptr<WebPAnimDecoder, void (*)(WebPAnimDecoder *)> decoder(WebPAnimDecoderNew(&data, &options),
                                                                           WebPAnimDecoderDelete);
    uint8_t *frame = nullptr;
    int timestamp = 0;
    if (!decoder || !WebPAnimDecoderGetNext(decoder.get(), &frame, &timestamp) || !frame) {
        *error = invalid;
        return false;
    }
    const std::size_t bytes = std::size_t(pixels) * 4;
    out->data.reset(new unsigned char[bytes]);
    std::memcpy(out->data.get(), frame, bytes);
    out->width = w;
    out->height = h;
    out->sample = Decoded::Sample::U8;
    out->channels = 4;
    out->alphaIndex = (flags & ALPHA_FLAG) ? 3 : -1;
    out->sourceChannels = (flags & ALPHA_FLAG) ? 4 : 3;
    out->bits = 8;
    out->codec = QStringLiteral("libwebp");
    return true;
}
#endif // IMAGEVIEWER_HAVE_LIBWEBP

// The whole file in memory for decoders that parse from a buffer: mapped when the system
// allows it, read otherwise.
class FileBytes {
public:
    explicit FileBytes(QFile &file)
    {
        const qint64 size = file.size();
        if (size <= 0)
            return;
        if (uchar *mapped = file.map(0, size)) {
            m_view = QByteArrayView(mapped, size);
        } else if (file.seek(0)) {
            m_copy = file.readAll();
            m_view = m_copy;
        }
    }
    QByteArrayView view() const { return m_view; }

private:
    QByteArray m_copy;
    QByteArrayView m_view;
};

bool decodeWith(Decoder decoder, const QString &path, QFile &file, qint64 maxPixels, Decoded *out, QString *error)
{
    switch (decoder) {
    case Decoder::Jxl:
#ifdef IMAGEVIEWER_HAVE_LIBJXL
        return decodeJxl(FileBytes(file).view(), maxPixels, out, error);
#else
        break;
#endif
    case Decoder::WebP:
#ifdef IMAGEVIEWER_HAVE_LIBWEBP
        return decodeWebP(FileBytes(file).view(), maxPixels, out, error);
#else
        break;
#endif
    case Decoder::Qt: return decodeWithQt(path, maxPixels, out, error);
    case Decoder::OpenImageIO: break;
    }
    return decodeWithOiio(path, maxPixels, out, error);
}

} // namespace

bool decodeFile(const QString &path, qint64 maxPixels, Decoded *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    // Detection by content (D-38): the format's own back end first, then the general ones.
    const QByteArray head = file.read(kFormatHeadBytes);
    const Format *format = detectFormat(head, QFileInfo(path).suffix());
    QList<Decoder> order;
    if (format && isAvailable(*format))
        order << format->decoder;
    for (Decoder general : {Decoder::OpenImageIO, Decoder::Qt})
        if (!order.contains(general))
            order << general;
    QString firstError;
    for (Decoder decoder : std::as_const(order)) {
        *out = Decoded(); // nothing from a failed attempt may leak into the next one
        QString why;
        if (decodeWith(decoder, path, file, maxPixels, out, &why))
            return true;
        if (out->overLimit)
            return false;
        if (firstError.isEmpty())
            firstError = why;
    }
    *error = firstError;
    return false;
}

void shutdownOpenImageIO()
{
#if OIIO_VERSION >= OIIO_MAKE_VERSION(3, 0, 0)
    OIIO::shutdown();
#endif
}
