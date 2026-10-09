#include "decoders.h"

#include <OpenImageIO/filesystem.h>
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
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>


namespace {

using color::Descriptor;
using color::Transfer;

constexpr qint64 kWorkingBytesPerPixel = 16;    // RGBA16F result plus one orient/downscale copy


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
            // The range flag describes the stored samples. libheif hands OpenImageIO RGB it
            // has already expanded from narrow-range YCbCr, so for HEIF/AVIF it is always full.
            d->fullRange = v[3] != 0 || std::strcmp(format, "heif") == 0;
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
    // Netpbm files carry no colour metadata, but OIIO labels every variant "Rec709". PFM is
    // linear by convention (HDR radiance maps; as BT.1886, 36.0 became 5434, D-17), and the
    // integer variants hold sRGB values in practice, as other viewers read them (D-54).
    const bool pnm = std::strcmp(format, "pnm") == 0;
    const QString cs = pnm ? QString() : QString::fromStdString(spec.get_string_attribute("oiio:ColorSpace"));
    bool known = parseOiioColorSpace(cs, d);
    // A gamma name ("g22_rec709", "Gamma2.2") is rounded, and odd exponents (PNG gAMA of γ 0.5 or
    // 12.5) give names it cannot parse: the exact exponent is in "oiio:Gamma".
    static const QRegularExpression gammaName(QStringLiteral("^(g\\d|gamma)"), QRegularExpression::CaseInsensitiveOption);
    const float gamma = spec.get_float_attribute("oiio:Gamma", 0.0f);
    if (gamma >= 0.1f && gamma <= 20.0f && ((known && d->transfer == Transfer::Power) || (!known && gammaName.match(cs).hasMatch()))) {
        if (!known)
            d->primaries = color::kBt709;
        d->transfer = Transfer::Power;
        d->gamma = gamma;
        known = true;
    }
    if (known) {
        // OIIO also fills this in when the file carries no colour tag at all, so it
        // is reported as the decoder's interpretation, not as file metadata (F12).
        d->source = Descriptor::Source::FormatAttributes;
        d->description = QCoreApplication::translate("Image", "%1 (assigned by the decoder)").arg(cs);
        return;
    }
    assumeDefault(isFloat, d);
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

// A file read into memory for OpenImageIO, which reads it through `reader`: it must outlive
// every ImageInput opened on it. Animations are read this way, so that no file stays open
// while they play (Windows could neither rename nor delete it).
struct OiioMemory {
    explicit OiioMemory(QByteArray content) : bytes(std::move(content)), reader(bytes.data(), std::size_t(bytes.size())) {}
    QByteArray bytes;
    OIIO::Filesystem::IOMemReader reader;
};

// Defined after OiioFrames: the frames after the first of an animated GIF.
std::unique_ptr<FrameReader> makeOiioFrames(std::unique_ptr<OIIO::ImageInput> in, std::unique_ptr<OiioMemory> memory,
                                            const std::string &name, const Decoded &first, OIIO::TypeDesc request,
                                            int loopCount);
int oiioFrameDurationMs(const OIIO::ImageSpec &spec);

OIIO::ImageSpec oiioConfig()
{
    OIIO::ImageSpec config;
    // Straight alpha where the format stores it (PNG, TIFF, WebP, HEIF...): OIIO would
    // otherwise premultiply the encoded values, which is wrong for non-linear data.
    config.attribute("oiio:UnassociatedAlpha", 1);
    // PFM stores rows bottom to top; OIIO 3.1 flips them only when asked (pnminput.cpp).
    config.attribute("pnm:pfmflip", 1);
    return config;
}

// Opens a file held in memory (the name only tells OpenImageIO the format).
std::unique_ptr<OIIO::ImageInput> openOiioMemory(OiioMemory &memory, const std::string &name)
{
    OIIO::Filesystem::IOProxy *proxy = &memory.reader;
    proxy->seek(0);
    OIIO::ImageSpec config = oiioConfig();
    // With a configuration, OpenImageIO's readers take the proxy from it, not from the argument.
    config.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &proxy);
    return OIIO::ImageInput::open(name, &config, proxy);
}

// Reads the first subimage of an opened file. With `frames`, a file of several subimages is
// read as an animation (GIF); `memory`, if the file was opened from memory, then goes to the
// frame reader.
bool readOiio(std::unique_ptr<OIIO::ImageInput> in, qint64 maxPixels, Decoded *out, QString *error,
              std::unique_ptr<FrameReader> *frames, std::unique_ptr<OiioMemory> *memory = nullptr)
{
    const OIIO::ImageSpec &spec = in->spec();
    const int w = spec.width, h = spec.height, nch = spec.nchannels;
    // A volume (FITS cube, TIFF ImageDepth, 3D DDS texture) is read whole by read_image(): the
    // buffer holds every slice, and its first slice is shown.
    const int depth = std::max(spec.depth, 1);
    if (w <= 0 || h <= 0 || nch <= 0 || qint64(w) * h > kMaxPixels || qint64(w) * h * depth > kMaxPixels) {
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
    // The GIF reader names an alpha channel "A" but gives its index as 4 of 4.
    int alpha = spec.alpha_channel >= 0 && spec.alpha_channel < nch ? spec.alpha_channel : -1;
    for (int c = 0; alpha < 0 && c < nch && c < int(spec.channelnames.size()); ++c)
        if (spec.channelnames[std::size_t(c)] == "A")
            alpha = c;
    const bool gray = nch < 3;
    const int readChannels = std::max(gray ? 1 : 3, alpha + 1);
    const qint64 pixels = qint64(w) * h * depth;
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
    // Alpha is straight (D-21) except where the format stores colour premultiplied: OpenEXR
    // always, TIFF when its ExtraSamples says so and HEIF/AVIF when the file says so (straight
    // alpha of those comes marked "oiio:UnassociatedAlpha", as asked in oiioConfig()). Other
    // readers keep the file's straight values without always saying so (BMP, DDS, ICO, SGI,
    // JPEG 2000).
    const std::string_view format = in->format_name();
    out->associatedAlpha = alpha >= 0
                           && (format == "openexr"
                               || ((format == "tiff" || format == "heif")
                                   && spec.get_int_attribute("oiio:UnassociatedAlpha", 0) == 0));
    out->gray = gray;
    // Some readers give the bits of a whole pixel (DDS: 32 for 8-bit RGBA).
    out->bits = std::min(spec.get_int_attribute("oiio:BitsPerSample", int(stored.size() * 8)), int(stored.size() * 8));
    out->orientation = spec.get_int_attribute("Orientation", 1);
    out->codec = QStringLiteral("OpenImageIO/%1").arg(QString::fromUtf8(in->format_name()));
    out->camera = cameraFromOiio(spec);
    describeOiio(spec, !out->isInteger(), in->format_name(), &out->colour);
    if (frames) {
        const int durationMs = oiioFrameDurationMs(spec);
        // Loops as browsers play a GIF: without a NETSCAPE block once, with a count N (the
        // repeats after the first play) N + 1 times, with 0 forever.
        const bool counted = spec.find_attribute("gif:LoopCount") || spec.find_attribute("oiio:LoopCount");
        const int repeats = spec.get_int_attribute("gif:LoopCount", spec.get_int_attribute("oiio:LoopCount", 0));
        const int loop = !counted ? 1 : repeats <= 0 ? 0 : repeats + 1;
        if (in->seek_subimage(1, 0)) {
            out->durationMs = durationMs;
            const std::string name = "image." + std::string(in->format_name()); // tells OpenImageIO the format
            *frames = makeOiioFrames(std::move(in), memory ? std::move(*memory) : nullptr, name, *out, request, loop);
        } else {
            (void)in->geterror(); // a single image: no frames, and no error left behind
        }
    }
    return true;
}

bool decodeWithOiio(const QString &path, qint64 maxPixels, Decoded *out, QString *error,
                    std::unique_ptr<FrameReader> *frames = nullptr)
{
    if (frames) { // an animation: read from memory, see OiioMemory
        QByteArray bytes;
        if (!readWholeFile(path, &bytes, error))
            return false;
        auto memory = std::make_unique<OiioMemory>(std::move(bytes));
        auto in = openOiioMemory(*memory, QFile::encodeName(path).toStdString());
        if (!in) {
            *error = QString::fromStdString(OIIO::geterror());
            return false;
        }
        return readOiio(std::move(in), maxPixels, out, error, frames, &memory);
    }
    const OIIO::ImageSpec config = oiioConfig();
    auto in = OIIO::ImageInput::open(path.toUtf8().toStdString(), &config);
    if (!in) {
        *error = QString::fromStdString(OIIO::geterror());
        return false;
    }
    return readOiio(std::move(in), maxPixels, out, error, frames);
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





// The frames after the first of an animated GIF, composited by OpenImageIO (its subimages),
// read from the file's bytes in memory.
class OiioFrames final : public FrameReader {
public:
    OiioFrames(std::unique_ptr<OIIO::ImageInput> in, std::unique_ptr<OiioMemory> memory, std::string name,
               const Decoded &first, OIIO::TypeDesc request, int loopCount)
        : m_memory(std::move(memory)), m_name(std::move(name)), m_in(std::move(in)), m_request(request), m_loop(loopCount)
    {
        copyLayout(first, &m_layout);
    }

    bool next(Decoded *out, int *durationMs, QString *error) override
    {
        if (!m_in->seek_subimage(m_next, 0)) {
            (void)m_in->geterror(); // past the last frame: not an error
            m_count = m_next;
            return false;
        }
        const OIIO::ImageSpec &spec = m_in->spec();
        if (spec.width != m_layout.width || spec.height != m_layout.height || spec.nchannels < m_layout.channels
            || spec.depth > 1) { // a volume would not fit the frame buffer
            *error = damaged(m_in->format_name());
            return false;
        }
        copyLayout(m_layout, out);
        out->data.reset(new unsigned char[frameBytes(m_layout)]);
        if (!m_in->read_image(m_next, 0, 0, m_layout.channels, m_request, out->data.get())) {
            *error = QString::fromStdString(m_in->geterror());
            return false;
        }
        *durationMs = oiioFrameDurationMs(spec);
        ++m_next;
        return true;
    }

    bool rewind(QString *error) override
    {
        // Opened again rather than sought back: OpenImageIO's GIF reader would reopen the
        // file by its name, which may have been renamed or deleted meanwhile.
        if (m_memory) {
            m_in.reset();
            m_in = openOiioMemory(*m_memory, m_name);
            if (!m_in) {
                *error = QString::fromStdString(OIIO::geterror());
                return false;
            }
        }
        m_next = 0;
        return true;
    }

    int frameCount() const override { return m_count; }
    int loopCount() const override { return m_loop; }

private:
    std::unique_ptr<OiioMemory> m_memory; // declared first: destroyed after m_in
    std::string m_name;
    std::unique_ptr<OIIO::ImageInput> m_in;
    OIIO::TypeDesc m_request;
    Decoded m_layout;
    int m_next = 1;
    int m_count = 0;
    int m_loop = 0;
};

std::unique_ptr<FrameReader> makeOiioFrames(std::unique_ptr<OIIO::ImageInput> in, std::unique_ptr<OiioMemory> memory,
                                            const std::string &name, const Decoded &first, OIIO::TypeDesc request,
                                            int loopCount)
{
    return std::make_unique<OiioFrames>(std::move(in), std::move(memory), name, first, request, loopCount);
}

// GIF delays, in hundredths of a second, as OpenImageIO reports them.
int oiioFrameDurationMs(const OIIO::ImageSpec &spec)
{
    if (const OIIO::ParamValue *p = spec.find_attribute("FramesPerSecond"); p && p->type() == OIIO::TypeRational) {
        const int *fps = static_cast<const int *>(p->data()); // numerator, denominator
        if (fps[0] > 0 && fps[1] >= 0)
            return playableMs(1000.0 * fps[1] / fps[0]);
    }
    return 100;
}

bool decodeWith(Decoder decoder, const QString &path, const Format *format, qint64 maxPixels, Decoded *out,
                QString *error, std::unique_ptr<FrameReader> *frames)
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
    case Decoder::Qt: return decodeWithQt(path, maxPixels, out, error);
    case Decoder::System: return decodeSystemHeic(path, maxPixels, out, error);
    case Decoder::GraphicsMagick:
        if (!format)
            break;
        return decodeInWorker(*format, path, maxPixels, out, error);
    case Decoder::OpenImageIO: break;
    }
    // GIF animations are OpenImageIO's subimages; pages of other formats are not frames.
    const bool animatable = frames && format && format->decoder == Decoder::OpenImageIO && (format->capabilities & CanAnimate);
    if (!decodeWithOiio(path, maxPixels, out, error, animatable ? frames : nullptr))
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
    if (file.size() > (qint64(1) << 31)) {
        //: GB: gigabytes.
        *error = QCoreApplication::translate("Image", "image too large for the available memory (needs %1 GB, limit %2 GB)")
                     .arg(QLocale().toString(double(file.size()) / (1 << 30), 'f', 1), QLocale().toString(2.0, 'f', 1));
        return false;
    }
    *bytes = file.readAll();
    if (bytes->size() != file.size()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool decodeOiioMemory(QByteArrayView bytes, const char *name, Decoded *out, QString *error)
{
    OIIO::Filesystem::IOMemReader memory(bytes.data(), std::size_t(bytes.size())); // outlives the reader below
    OIIO::Filesystem::IOProxy *proxy = &memory;
    OIIO::ImageSpec config = oiioConfig();
    // With a configuration, OpenImageIO's readers take the proxy from it, not from the argument.
    config.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &proxy);
    auto in = OIIO::ImageInput::open(name, &config, proxy);
    if (!in) {
        *error = QString::fromStdString(OIIO::geterror());
        return false;
    }
    return readOiio(std::move(in), 0, out, error, nullptr);
}

bool decodeFile(const QString &path, qint64 maxPixels, Decoded *out, QString *error, std::unique_ptr<FrameReader> *frames)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    // Detection by content (D-38): the format's own back end first, then the general ones.
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
    for (Decoder general : {Decoder::OpenImageIO, Decoder::Qt})
        if (!order.contains(general) && !(notOiio && general == Decoder::OpenImageIO))
            order << general;
    QString firstError;
    for (Decoder decoder : std::as_const(order)) {
        *out = Decoded(); // nothing from a failed attempt may leak into the next one
        if (frames)
            frames->reset();
        QString why;
        if (decodeWith(decoder, path, format, maxPixels, out, &why, frames))
            return true;
        if (out->overLimit)
            return false;
        if (firstError.isEmpty())
            firstError = why;
    }
    if (frames)
        frames->reset();
    // A HEIC file where the system has no HEVC decoder (and OpenImageIO's libheif has none
    // either): say how to get one rather than why the last decoder failed.
    const bool noSystemCodec = format && format->decoder == Decoder::System && !isAvailable(*format);
    *error = noSystemCodec ? missingHevcMessage() : firstError;
    return false;
}

void shutdownOpenImageIO()
{
#if OIIO_VERSION >= OIIO_MAKE_VERSION(3, 0, 0)
    OIIO::shutdown();
#endif
}
