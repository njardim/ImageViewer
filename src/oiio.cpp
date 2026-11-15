// The OpenImageIO back end (decision D-38): most still formats, camera RAW (LibRaw), EXR, DPX and
// HEIF/AVIF stills, GIF animations, the colour metadata OpenImageIO attaches, and the CMYK ink of
// TIFF and Photoshop files (D-100). Split from decoders.cpp (D-08), which keeps what every back end
// shares and the choice among them.
#include "decoders.h"

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/tiffutils.h>

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {

using color::Descriptor;
using color::Transfer;

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

// The ink of a CMYK image as stored (TIFF and Photoshop files opened with "oiio:RawColor"), rather
// than the RGB OpenImageIO otherwise makes of it.
bool isRawCmyk(const OIIO::ImageSpec &spec)
{
    const auto &n = spec.channelnames;
    return spec.nchannels >= 4 && n.size() >= 4 && n[0] == "C" && n[1] == "M" && n[2] == "Y" && n[3] == "K";
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
        // The range flag describes the stored samples. libheif hands OpenImageIO RGB it
        // has already expanded from narrow-range YCbCr, so for HEIF/AVIF it is always full.
        if (describeCicp(v[0], v[1], v[2], v[3], v[3] != 0 || std::strcmp(format, "heif") == 0, d))
            return;
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("ICCProfile");
        p && p->type().basetype == OIIO::TypeDesc::UINT8 && p->type().size() > 0) {
        const QByteArray icc(static_cast<const char *>(p->data()), qsizetype(p->type().size()));
        // A CMYK profile describes the ink: of use only when the ink itself was read (isRawCmyk()); on
        // the RGB OpenImageIO made of it by the textbook formula, it is said as such.
        if (!isRawCmyk(spec) && color::isCmykProfile(icc)) {
            assumeDefault(isFloat, d);
            d->description = cmykWithoutProfile();
            return;
        }
        describeIcc(icc, d);
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
    // CMYK that OpenImageIO turned into RGB by the textbook formula (no CMYK profile to convert it with):
    // said as such, not as the sRGB OpenImageIO names.
    const std::string jpegSpace = spec.get_string_attribute("jpeg:ColorSpace");
    if (jpegSpace == "CMYK" || jpegSpace == "YCbCrK" || spec.get_string_attribute("tiff:ColorSpace") == "CMYK") {
        assumeDefault(isFloat, d);
        d->description = cmykWithoutProfile();
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
        d->description = QCoreApplication::translate("Image", "%1 (assigned by the decoder)").arg(cleanText(cs)); // the file may name it
        return;
    }
    assumeDefault(isFloat, d);
}

QString cleanText(const std::string &raw)
{
    return cleanText(QString::fromUtf8(raw.data(), qsizetype(std::min<std::size_t>(raw.size(), 256))));
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

// Opens bytes in memory, read through `proxy` from its start (the name only tells OpenImageIO
// the format, by its suffix).
std::unique_ptr<OIIO::ImageInput> openOiioProxy(OIIO::Filesystem::IOProxy *proxy, const std::string &name)
{
    proxy->seek(0);
    OIIO::ImageSpec config = oiioConfig();
    // With a configuration, OpenImageIO's readers take the proxy from it, not from the argument.
    config.attribute("oiio:ioproxy", OIIO::TypeDesc::PTR, &proxy);
    return OIIO::ImageInput::open(name, &config, proxy);
}

std::unique_ptr<OIIO::ImageInput> openOiioMemory(OiioMemory &memory, const std::string &name)
{
    return openOiioProxy(&memory.reader, name);
}

// A JPEG for a thumbnail of `side` pixels, read reduced (D-111): OpenImageIO decodes every pixel of
// a photograph for an image a few hundred pixels wide. libjpeg-turbo reads it at the smallest of its
// scales (1/8, 1/4, 1/2) that leaves at least twice the side asked for, so the reduction in linear
// light that follows (image.cpp) still averages, as for every thumbnail. The whole image, smaller;
// everything but the pixels (colour, orientation, shooting data) stays as OpenImageIO read it. Null,
// and the file is read whole as before, for anything but a plain 8-bit gray or colour JPEG that
// libjpeg-turbo reads without a warning; that decode's memory then grows from the reduced image's
// without waiting for other decodes (DecodeMemory), as rarely as a damaged JPEG has a thumbnail.
std::unique_ptr<unsigned char[]> readReducedJpeg(const QString &path, int side, int channels, int *width, int *height)
{
    const int longest = std::max(*width, *height);
    for (const int denominator : {8, 4, 2}) {
        const int w = (*width + denominator - 1) / denominator, h = (*height + denominator - 1) / denominator;
        if ((longest + denominator - 1) / denominator < 2 * qint64(side))
            continue;
        QByteArray file;
        QString ignored;
        if (!fitsInMemory(qint64(w) * h, channels, &ignored) || !readWholeFile(path, &file, &ignored))
            return nullptr;
        std::unique_ptr<unsigned char[]> pixels = decodeReducedJpeg(file, denominator, channels, w, h);
        if (pixels) {
            *width = w;
            *height = h;
        }
        return pixels;
    }
    return nullptr;
}

// Reads the first subimage of an opened file. With `frames`, a file of several subimages is
// read as an animation (GIF); `memory`, if the file was opened from memory, then goes to the
// frame reader.
// `reducePath` and `reduceSide`: the file and the side of the thumbnail it is read for (readReducedJpeg()).
bool readOiio(std::unique_ptr<OIIO::ImageInput> in, qint64 maxPixels, Decoded *out, QString *error,
              std::unique_ptr<FrameReader> *frames, std::unique_ptr<OiioMemory> *memory = nullptr,
              const QString *reducePath = nullptr, int reduceSide = 0)
{
    const OIIO::ImageSpec &spec = in->spec();
    int w = spec.width, h = spec.height;
    const int nch = spec.nchannels;
    // A volume (FITS cube, TIFF ImageDepth, 3D DDS texture) is read whole by read_image(): the
    // buffer holds every slice, and its first slice is shown.
    const int depth = std::max(spec.depth, 1);

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
    // The ink of a CMYK file comes first: alpha, if any, after it (OpenImageIO 2.4 calls K the alpha).
    const bool cmyk = isRawCmyk(spec);
    const int firstAlpha = cmyk ? 4 : 0;
    int alpha = spec.alpha_channel >= firstAlpha && spec.alpha_channel < nch ? spec.alpha_channel : -1;
    for (int c = firstAlpha; alpha < 0 && c < nch && c < int(spec.channelnames.size()); ++c)
        if (spec.channelnames[std::size_t(c)] == "A")
            alpha = c;
    const bool gray = nch < 3;
    const int readChannels = std::max(cmyk ? 4 : gray ? 1 : 3, alpha + 1);
    // Within the limits checkDimensions() sets on the whole image, which is the one over a limit.
    if (reducePath && reduceSide > 0 && in->format_name() == std::string_view("jpeg") && out->sample == Sample::U8
        && !cmyk && alpha < 0 && depth == 1 && (nch == 1 || nch == 3) && w > 0 && h > 0 && qint64(w) * h <= kMaxPixels
        && (maxPixels <= 0 || qint64(w) * h <= maxPixels))
        out->data = readReducedJpeg(*reducePath, reduceSide, nch, &w, &h);
    if (!out->data) {
        if (!checkDimensions(w, h, nch, readChannels * out->sampleBytes(), maxPixels, out, error, depth))
            return false;
        const qint64 pixels = qint64(w) * h * depth;
        out->data.reset(new unsigned char[std::size_t(pixels) * std::size_t(readChannels * out->sampleBytes())]);
        if (!in->read_image(0, 0, 0, readChannels, request, out->data.get())) {
            *error = libraryError("OpenImageIO:", in->geterror());
            return false;
        }
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
    out->cmyk = cmyk;
    out->inkInverted = cmyk && format == "psd"; // Photoshop stores 1 - ink; TIFF the ink itself
    if (cmyk)
        out->associatedAlpha = false; // a CMYK file's alpha is straight (OpenImageIO drops a TIFF's)
    // Some readers give the bits of a whole pixel (DDS: 32 for 8-bit RGBA).
    out->bits = std::min(spec.get_int_attribute("oiio:BitsPerSample", int(stored.size() * 8)), int(stored.size() * 8));
    // 1 to 8 only (F7): a JPEG's or PNG's EXIF may hold anything, and nothing else is applied or shown.
    const int orientation = spec.get_int_attribute("Orientation", 1);
    out->orientation = orientation >= 1 && orientation <= 8 ? orientation : 1;
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

} // namespace

bool decodeWithOiio(const QString &path, const Format *format, qint64 maxPixels, Decoded *out, QString *error,
                    std::unique_ptr<FrameReader> *frames, int thumbnailSide)
{
    if (frames && format) { // an animation: read from memory, see OiioMemory
        QByteArray bytes;
        if (!readWholeFile(path, &bytes, error))
            return false;
        auto memory = std::make_unique<OiioMemory>(std::move(bytes));
        // Named by its format, not by the file's name: a GIF named .jpg would go to the JPEG
        // reader first, and then to every other reader in turn.
        auto in = openOiioMemory(*memory, "image." + format->suffixes().constFirst().toStdString());
        if (!in) {
            *error = libraryError("OpenImageIO:", OIIO::geterror());
            return false;
        }
        return readOiio(std::move(in), maxPixels, out, error, frames, &memory);
    }
    const std::string name = path.toUtf8().toStdString();
    const OIIO::ImageSpec config = oiioConfig();
    auto in = OIIO::ImageInput::open(name, &config);
    if (!in) {
        *error = libraryError("OpenImageIO:", OIIO::geterror());
        return false;
    }
    // CMYK with a CMYK profile (D-100): its ink, converted through the profile, never the RGB
    // OpenImageIO makes of it by the textbook formula. A JPEG's ink comes from libjpeg-turbo (OpenImageIO
    // converts it in any case), a TIFF's or a Photoshop file's from OpenImageIO asked for it.
    if (const OIIO::ParamValue *p = in->spec().find_attribute("ICCProfile");
        p && p->type().basetype == OIIO::TypeDesc::UINT8 && !isRawCmyk(in->spec())
        && color::isCmykProfile(QByteArray(static_cast<const char *>(p->data()), qsizetype(p->type().size())))) {
        const std::string_view reader = in->format_name();
        if (reader == "jpeg") {
            QByteArray bytes;
            QString why;
            if (readWholeFile(path, &bytes, &why) && decodeCmykJpeg(bytes, maxPixels, out, &why))
                return true;
            if (out->overLimit) // the preload's limit: decoded when shown, not as RGB now
                return false;
            qCInfo(lcDecode).noquote() << "CMYK JPEG through libjpeg-turbo:" << why; // OpenImageIO's RGB then
            *out = Decoded();
        } else if (reader == "tiff" || reader == "psd") {
            OIIO::ImageSpec raw = oiioConfig();
            raw.attribute("oiio:RawColor", 1);
            if (auto ink = OIIO::ImageInput::open(name, &raw); ink && isRawCmyk(ink->spec()))
                in = std::move(ink);
            else
                (void)OIIO::geterror(); // another ink set: the RGB opened first
        }
    }
    return readOiio(std::move(in), maxPixels, out, error, frames, nullptr, &path, thumbnailSide);
}

// Whether this build's OpenImageIO reads HEIF from memory (3.x; 2.4 only from a file).
bool oiioReadsHeifFromMemory()
{
    static const bool reads = [] {
        const auto in = OIIO::ImageInput::create("heif");
        const bool yes = in && in->supports("ioproxy");
        if (!in)
            (void)OIIO::geterror();
        return yes;
    }();
    return reads;
}

// A thumbnail of a camera RAW file from the preview the camera embedded in it (the owner, D-100):
// a JPEG it rendered itself, decoded in a fraction of the time the raw data takes to develop. Only
// one at least `side` pixels on its longer side, so that the thumbnail is as sharp as a developed
// one; false (and nothing in *out) when there is none, and the raw data is developed instead. A
// preview over `maxPixels` sets out->overLimit: the raw data is larger still.
bool decodeRawPreview(const QString &path, int side, qint64 maxPixels, Decoded *out)
{
    const OIIO::ImageSpec config = oiioConfig();
    auto in = OIIO::ImageInput::open(path.toUtf8().toStdString(), &config);
    if (!in) {
        (void)OIIO::geterror();
        return false;
    }
    const OIIO::ImageSpec &spec = in->spec();
    const int width = spec.get_int_attribute("thumbnail_width", 0), height = spec.get_int_attribute("thumbnail_height", 0);
    if (!in->supports("thumbnail") || side <= 0 || std::max(width, height) < side)
        return false;
    QString error; // unused: the raw data is developed instead, or refused over the limit
    if (!checkDimensions(width, height, 4, 4, maxPixels, out, &error))
        return false;
    OIIO::ImageBuf preview;
    if (!in->get_thumbnail(preview, 0) || !preview.initialized()) {
        (void)in->geterror();
        return false;
    }
    const OIIO::ImageSpec &got = preview.spec();
    const int channels = got.nchannels;
    if ((channels != 3 && channels != 4) || got.width <= 0 || got.height <= 0
        || !checkDimensions(got.width, got.height, channels, 4, maxPixels, out, &error))
        return false;
    out->data.reset(new unsigned char[std::size_t(got.width) * std::size_t(got.height) * std::size_t(channels)]);
    if (!preview.get_pixels(OIIO::ROI::All(), OIIO::TypeDesc::UINT8, out->data.get())) {
        (void)preview.geterror();
        out->data.reset();
        return false;
    }
    out->width = got.width;
    out->height = got.height;
    out->sample = Decoded::Sample::U8;
    out->channels = channels;
    out->sourceChannels = channels;
    out->alphaIndex = channels == 4 ? 3 : -1;
    out->bits = 8;
    // The preview is stored as the sensor lies, and turned as the developed image is: by LibRaw's
    // flip, which OpenImageIO keeps as "raw:flip" (bit 2 transposes, bit 1 mirrors vertically,
    // bit 0 horizontally), its "Orientation" set to 1 for the image LibRaw has turned already.
    static constexpr int kExifOrientation[8] = {1, 2, 4, 3, 5, 8, 6, 7};
    const int flip = spec.get_int_attribute("raw:flip", 0);
    out->orientation = flip >= 0 && flip <= 7 ? kExifOrientation[flip] : 1;
    out->codec = QStringLiteral("OpenImageIO/raw (embedded preview)");
    out->camera = cameraFromOiio(spec);
    assumeDefault(false, &out->colour); // the camera's JPEG, in sRGB
    return true;
}

namespace {

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
            *error = libraryError("OpenImageIO:", m_in->geterror(), m_in->format_name());
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
                *error = libraryError("OpenImageIO:", OIIO::geterror(), m_name.substr(m_name.rfind('.') + 1));
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

} // namespace

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

bool decodeOiioMemory(QByteArrayView bytes, const char *name, Decoded *out, QString *error, qint64 maxPixels)
{
    OIIO::Filesystem::IOMemReader memory(bytes.data(), std::size_t(bytes.size())); // outlives the reader below
    // APNG's frames come through here: their errors must not be empty, so the name's suffix names the format.
    const std::string_view named(name);
    const std::string suffix(named.substr(named.rfind('.') + 1));
    auto in = openOiioProxy(&memory, name);
    if (!in) {
        *error = libraryError("OpenImageIO:", OIIO::geterror(), suffix);
        return false;
    }
    if (readOiio(std::move(in), maxPixels, out, error, nullptr))
        return true;
    if (error->isEmpty())
        *error = damaged(QString::fromStdString(suffix).toUpper().toUtf8().constData());
    return false;
}

void shutdownOpenImageIO()
{
#if OIIO_VERSION >= OIIO_MAKE_VERSION(3, 0, 0)
    OIIO::shutdown();
#endif
}
