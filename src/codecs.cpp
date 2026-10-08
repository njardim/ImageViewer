// Back ends that parse the file from memory (decision D-38): libjxl (JPEG XL), libwebp (WebP,
// its ICC profile and animation) and APNG, whose frames are rebuilt as plain PNGs for
// OpenImageIO. Shared helpers are in decoders.cpp.
#include "decoders.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
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

#endif // IMAGEVIEWER_HAVE_LIBJXL

#ifdef IMAGEVIEWER_HAVE_LIBJXL
// A JPEG XL file at its native depth, with straight alpha and the colour encoding of the
// decoded samples; an animation's frames come composited, one after another.
class JxlFrames final : public FrameReader {
public:
    // Reads the first frame into *first; nullptr (with *error or first->overLimit) on failure.
    static std::unique_ptr<JxlFrames> open(QByteArray bytes, qint64 maxPixels, Decoded *first, QString *error)
    {
        std::unique_ptr<JxlFrames> reader(new JxlFrames(std::move(bytes), maxPixels));
        int durationMs = 0;
        if (!reader->start(error) || !reader->readFrame(first, &durationMs, error)) {
            if (error->isEmpty() && !first->overLimit)
                *error = damaged("JPEG XL");
            return nullptr;
        }
        first->durationMs = durationMs;
        return reader;
    }

    bool isAnimated() const { return m_info.have_animation && m_count != 1; }

    bool next(Decoded *out, int *durationMs, QString *error) override
    {
        if (m_done)
            return false;
        copyLayout(m_layout, out);
        return readFrame(out, durationMs, error);
    }

    bool rewind(QString *error) override
    {
        JxlDecoderRewind(m_dec.get());
        m_done = false;
        m_index = 0;
        return start(error);
    }

    int frameCount() const override { return m_count; }
    int loopCount() const override { return int(std::min<uint32_t>(m_info.animation.num_loops, 1000000)); }

private:
    JxlFrames(QByteArray bytes, qint64 maxPixels) : m_bytes(std::move(bytes)), m_maxPixels(maxPixels) {}

    bool start(QString *error)
    {
        if (!m_dec) {
            m_dec = JxlDecoderMake(nullptr);
            m_runner = JxlThreadParallelRunnerMake(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
        }
        if (!m_dec || !m_runner
            || JxlDecoderSubscribeEvents(m_dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FRAME
                                                          | JXL_DEC_FULL_IMAGE | (m_haveInfo ? 0 : JXL_DEC_BOX))
                   != JXL_DEC_SUCCESS
            || JxlDecoderSetParallelRunner(m_dec.get(), JxlThreadParallelRunner, m_runner.get()) != JXL_DEC_SUCCESS
            || JxlDecoderSetKeepOrientation(m_dec.get(), JXL_TRUE) != JXL_DEC_SUCCESS // applied once, by image.cpp
            || JxlDecoderSetUnpremultiplyAlpha(m_dec.get(), JXL_TRUE) != JXL_DEC_SUCCESS
            || JxlDecoderSetDecompressBoxes(m_dec.get(), JXL_TRUE) != JXL_DEC_SUCCESS
            || JxlDecoderSetInput(m_dec.get(), reinterpret_cast<const uint8_t *>(m_bytes.constData()), std::size_t(m_bytes.size()))
                   != JXL_DEC_SUCCESS) {
            *error = damaged("JPEG XL");
            return false;
        }
        JxlDecoderCloseInput(m_dec.get());
        return true;
    }

    // Header, colour encoding and EXIF on the first pass; later passes only check them.
    bool readInfo(Decoded *out, QString *error)
    {
        JxlBasicInfo info{};
        if (JxlDecoderGetBasicInfo(m_dec.get(), &info) != JXL_DEC_SUCCESS) {
            *error = damaged("JPEG XL");
            return false;
        }
        if (m_haveInfo)
            return true;
        m_info = info;
        const qint64 pixels = qint64(info.xsize) * info.ysize;
        if (info.xsize == 0 || info.ysize == 0 || pixels > kMaxPixels) {
            *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)")
                         .arg(info.xsize).arg(info.ysize).arg(info.num_color_channels);
            return false;
        }
        if (m_maxPixels > 0 && pixels > m_maxPixels) {
            out->overLimit = true;
            return false;
        }
        using Sample = Decoded::Sample;
        // Lossy files are coded in XYB: libjxl turns them into the original colour space when
        // it can describe it, and otherwise into linear sRGB, which 8 or 16-bit integers would
        // band and clip to the sRGB gamut. Floats keep both.
        const bool xyb = !info.uses_original_profile;
        out->sample = info.exponent_bits_per_sample > 0 || xyb ? (info.bits_per_sample <= 16 ? Sample::F16 : Sample::F32)
                      : info.bits_per_sample <= 8               ? Sample::U8
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
        m_format = {uint32_t(out->channels), type, JXL_NATIVE_ENDIAN, 0};
        return fitsInMemory(pixels, out->channels * out->sampleBytes(), error);
    }

    void readColour(Decoded *out)
    {
        // The encoding of the samples the decoder outputs (for lossy XYB files, the one the
        // image was encoded from).
        JxlColorEncoding encoding{};
        if (JxlDecoderGetColorAsEncodedProfile(JXL_COLOR_ARGS(m_dec.get()), &encoding) == JXL_DEC_SUCCESS
            && describeJxl(encoding, &out->colour))
            return;
        std::size_t size = 0;
        if (JxlDecoderGetICCProfileSize(JXL_COLOR_ARGS(m_dec.get()), &size) == JXL_DEC_SUCCESS && size > 0
            && size < (std::size_t(1) << 26)) {
            QByteArray icc(qsizetype(size), Qt::Uninitialized);
            if (JxlDecoderGetColorAsICCProfile(JXL_COLOR_ARGS(m_dec.get()), reinterpret_cast<uint8_t *>(icc.data()), size)
                == JXL_DEC_SUCCESS) {
                describeIcc(icc, &out->colour);
                return;
            }
        }
        assumeDefault(!out->isInteger(), &out->colour);
    }

    void finishExif(Decoded *out)
    {
        if (!m_readingExif)
            return;
        m_exif.resize(m_exif.size() - JxlDecoderReleaseBoxBuffer(m_dec.get()));
        m_readingExif = false;
        if (m_exif.size() > 4) { // a 4-byte offset, then the TIFF header
            const std::size_t offset = std::size_t(m_exif[0]) << 24 | std::size_t(m_exif[1]) << 16
                                       | std::size_t(m_exif[2]) << 8 | m_exif[3];
            if (offset < m_exif.size() - 4) // orientation comes from the codestream; EXIF's is ignored
                out->camera = cameraFromExif(QByteArrayView(m_exif.data() + 4 + offset, qsizetype(m_exif.size() - 4 - offset)), nullptr);
        }
        m_exif.clear();
    }

    bool readFrame(Decoded *out, int *durationMs, QString *error)
    {
        const bool first = !m_haveInfo;
        bool haveFrame = false;
        for (;;) {
            switch (JxlDecoderProcessInput(m_dec.get())) {
            case JXL_DEC_BASIC_INFO:
                if (!readInfo(out, error))
                    return false;
                break;
            case JXL_DEC_COLOR_ENCODING:
                if (first)
                    readColour(out);
                break;
            case JXL_DEC_FRAME: {
                JxlFrameHeader header{};
                if (JxlDecoderGetFrameHeader(m_dec.get(), &header) != JXL_DEC_SUCCESS) {
                    *error = damaged("JPEG XL");
                    return false;
                }
                const double tps = m_info.animation.tps_denominator > 0
                                       ? double(m_info.animation.tps_numerator) / m_info.animation.tps_denominator
                                       : 0.0;
                *durationMs = m_info.have_animation && tps > 0 ? playableMs(header.duration * 1000.0 / tps) : 0;
                if (header.is_last)
                    m_count = m_index + 1;
                break;
            }
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
                std::size_t bytes = 0;
                if (JxlDecoderImageOutBufferSize(m_dec.get(), &m_format, &bytes) != JXL_DEC_SUCCESS || bytes != frameBytes(*out)) {
                    *error = damaged("JPEG XL");
                    return false;
                }
                out->data.reset(new unsigned char[bytes]);
                if (JxlDecoderSetImageOutBuffer(m_dec.get(), &m_format, out->data.get(), bytes) != JXL_DEC_SUCCESS) {
                    *error = damaged("JPEG XL");
                    return false;
                }
                break;
            }
            case JXL_DEC_BOX: {
                finishExif(out);
                JxlBoxType type{};
                if (first && JxlDecoderGetBoxType(m_dec.get(), type, JXL_TRUE) == JXL_DEC_SUCCESS
                    && std::memcmp(type, "Exif", 4) == 0) {
                    m_exif.assign(4096, 0);
                    m_readingExif = JxlDecoderSetBoxBuffer(m_dec.get(), m_exif.data(), m_exif.size()) == JXL_DEC_SUCCESS;
                }
                break;
            }
            case JXL_DEC_BOX_NEED_MORE_OUTPUT: {
                const std::size_t used = m_exif.size() - JxlDecoderReleaseBoxBuffer(m_dec.get());
                if (m_exif.size() >= (std::size_t(1) << 20)) { // not a real EXIF block: stop collecting it
                    m_readingExif = false;
                    m_exif.clear();
                    break;
                }
                m_exif.resize(m_exif.size() * 2);
                // The new buffer ends where the vector ends, so finishExif()'s arithmetic holds.
                m_readingExif = JxlDecoderSetBoxBuffer(m_dec.get(), m_exif.data() + used, m_exif.size() - used) == JXL_DEC_SUCCESS;
                break;
            }
            case JXL_DEC_FULL_IMAGE:
                ++m_index;
                if (first) {
                    m_haveInfo = true;
                    m_layout = Decoded();
                    copyLayout(*out, &m_layout);
                }
                if (m_info.have_animation) {
                    finishExif(out);
                    return true;
                }
                haveFrame = true; // a still image: read on for EXIF boxes after the codestream
                break;
            case JXL_DEC_SUCCESS:
                finishExif(out);
                m_done = true;
                if (haveFrame)
                    m_count = 1;
                return haveFrame; // false without an error: the animation has ended
            case JXL_DEC_NEED_MORE_INPUT:
            case JXL_DEC_ERROR:
            default:
                *error = damaged("JPEG XL");
                return false;
            }
        }
    }

    QByteArray m_bytes; // the decoder reads from it
    qint64 m_maxPixels = 0;
    JxlDecoderPtr m_dec;
    JxlThreadParallelRunnerPtr m_runner;
    JxlBasicInfo m_info{};
    JxlPixelFormat m_format{};
    Decoded m_layout; // the first frame's layout, without samples
    bool m_haveInfo = false;
    bool m_done = false;
    int m_index = 0; // frames read since the start
    int m_count = 0;
    std::vector<uint8_t> m_exif;
    bool m_readingExif = false;
};
#undef JXL_COLOR_ARGS
#endif // IMAGEVIEWER_HAVE_LIBJXL

#ifdef IMAGEVIEWER_HAVE_LIBWEBP
// A WebP file, 8-bit straight RGBA, with the ICC profile and EXIF it carries (OpenImageIO
// assumes sRGB); an animation's frames come composited by libwebp.
class WebPFrames final : public FrameReader {
public:
    static std::unique_ptr<WebPFrames> open(QByteArray bytes, qint64 maxPixels, Decoded *first, QString *error)
    {
        std::unique_ptr<WebPFrames> reader(new WebPFrames(std::move(bytes)));
        if (!reader->start(maxPixels, first, error))
            return nullptr;
        if (!reader->next(first, &first->durationMs, error)) {
            if (error->isEmpty())
                *error = damaged("WebP");
            return nullptr;
        }
        return reader;
    }

    bool isAnimated() const { return m_info.frame_count > 1; }

    bool next(Decoded *out, int *durationMs, QString *error) override
    {
        if (!WebPAnimDecoderHasMoreFrames(m_decoder.get()))
            return false;
        uint8_t *frame = nullptr;
        int timestamp = 0; // when this frame ends, in ms from the start
        if (!WebPAnimDecoderGetNext(m_decoder.get(), &frame, &timestamp) || !frame) {
            *error = damaged("WebP");
            return false;
        }
        copyLayout(m_layout, out);
        const std::size_t bytes = frameBytes(m_layout);
        out->data.reset(new unsigned char[bytes]);
        std::memcpy(out->data.get(), frame, bytes);
        *durationMs = isAnimated() ? playableMs(timestamp - m_previousTimestamp) : 0;
        m_previousTimestamp = timestamp;
        return true;
    }

    bool rewind(QString *) override
    {
        WebPAnimDecoderReset(m_decoder.get());
        m_previousTimestamp = 0;
        return true;
    }

    int frameCount() const override { return int(m_info.frame_count); }
    int loopCount() const override { return int(m_info.loop_count); }

private:
    explicit WebPFrames(QByteArray bytes) : m_bytes(std::move(bytes)), m_decoder(nullptr, WebPAnimDecoderDelete) {}

    bool start(qint64 maxPixels, Decoded *first, QString *error)
    {
        const QString invalid = damaged("WebP");
        const WebPData data{reinterpret_cast<const uint8_t *>(m_bytes.constData()), std::size_t(m_bytes.size())};
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
            first->overLimit = true;
            return false;
        }
        if (!fitsInMemory(pixels, 4, error))
            return false;
        WebPChunkIterator chunk;
        if ((flags & ICCP_FLAG) && WebPDemuxGetChunk(demux.get(), "ICCP", 1, &chunk)) {
            describeIcc(QByteArray(reinterpret_cast<const char *>(chunk.chunk.bytes), qsizetype(chunk.chunk.size)), &first->colour);
            WebPDemuxReleaseChunkIterator(&chunk);
        } else {
            assumeDefault(false, &first->colour); // the WebP specification's default
        }
        if ((flags & EXIF_FLAG) && WebPDemuxGetChunk(demux.get(), "EXIF", 1, &chunk)) {
            QByteArrayView exif(chunk.chunk.bytes, qsizetype(chunk.chunk.size));
            if (exif.startsWith("Exif\0\0"))
                exif = exif.sliced(6);
            first->camera = cameraFromExif(exif, &first->orientation);
            WebPDemuxReleaseChunkIterator(&chunk);
        }

        WebPAnimDecoderOptions options;
        if (!WebPAnimDecoderOptionsInit(&options)) {
            *error = invalid;
            return false;
        }
        options.color_mode = MODE_RGBA; // straight alpha
        options.use_threads = 1;
        m_decoder.reset(WebPAnimDecoderNew(&data, &options));
        if (!m_decoder || !WebPAnimDecoderGetInfo(m_decoder.get(), &m_info) || int(m_info.canvas_width) != w
            || int(m_info.canvas_height) != h) {
            *error = invalid;
            return false;
        }
        first->width = w;
        first->height = h;
        first->sample = Decoded::Sample::U8;
        first->channels = 4;
        first->alphaIndex = (flags & ALPHA_FLAG) ? 3 : -1;
        first->sourceChannels = (flags & ALPHA_FLAG) ? 4 : 3;
        first->bits = 8;
        first->codec = QStringLiteral("libwebp");
        copyLayout(*first, &m_layout);
        return true;
    }

    QByteArray m_bytes; // libwebp reads from it
    std::unique_ptr<WebPAnimDecoder, void (*)(WebPAnimDecoder *)> m_decoder;
    WebPAnimInfo m_info{};
    Decoded m_layout;
    int m_previousTimestamp = 0;
};
#endif // IMAGEVIEWER_HAVE_LIBWEBP

quint32 bigEndian32(const unsigned char *p)
{
    return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | quint32(p[3]);
}

quint16 bigEndian16(const unsigned char *p)
{
    return quint16(quint16(p[0]) << 8 | p[1]);
}

void appendBigEndian32(QByteArray *out, quint32 v)
{
    const char bytes[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
    out->append(bytes, 4);
}

// The CRC-32 of PNG chunks (ISO 3309, polynomial 0xEDB88320).
quint32 crc32(QByteArrayView a, QByteArrayView b = {})
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (QByteArrayView part : {a, b})
        for (char byte : part)
            c = table[(c ^ quint8(byte)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void appendChunk(QByteArray *png, const char type[4], QByteArrayView data)
{
    appendBigEndian32(png, quint32(data.size()));
    png->append(type, 4);
    png->append(data.data(), data.size());
    appendBigEndian32(png, crc32(QByteArrayView(type, 4), data));
}

// Animated PNG. Each frame is rebuilt as a plain PNG (the file's own header and colour
// chunks, the frame's image data) and decoded by OpenImageIO at its native depth; frames are
// composited on the canvas with the dispose and blend operations of the APNG specification.
// Every chunk is checked against the file before it is used: lengths, regions inside the
// canvas, known operations.
class ApngFrames final : public FrameReader {
public:
    // nullptr with *error empty: a plain PNG (no animation control), read as such elsewhere.
    static std::unique_ptr<ApngFrames> open(QByteArray bytes, qint64 maxPixels, Decoded *first, QString *error)
    {
        std::unique_ptr<ApngFrames> reader(new ApngFrames(std::move(bytes)));
        if (!reader->parse(error) || reader->m_frames.size() < 2)
            return nullptr;
        const qint64 pixels = qint64(reader->m_width) * reader->m_height;
        if (pixels > kMaxPixels) {
            *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(reader->m_width).arg(reader->m_height).arg(4);
            return nullptr;
        }
        if (maxPixels > 0 && pixels > maxPixels) {
            first->overLimit = true;
            return nullptr;
        }
        if (!fitsInMemory(pixels, 8, error) || !reader->next(first, &first->durationMs, error))
            return nullptr;
        return reader;
    }

    bool next(Decoded *out, int *durationMs, QString *error) override
    {
        if (m_next >= int(m_frames.size()))
            return false;
        const Frame &frame = m_frames[std::size_t(m_next)];
        Decoded part;
        if (!decodeOiioMemory(frameAsPng(frame), "frame.png", &part, error))
            return false;
        if (part.width != int(frame.width) || part.height != int(frame.height)
            || (m_next > 0 && (part.channels != m_layout.channels || part.sample != m_layout.sample))) {
            *error = damaged("APNG");
            return false;
        }
        if (m_next == 0) { // the canvas takes the first frame's samples and channels
            copyLayout(part, &m_layout);
            m_layout.width = int(m_width);
            m_layout.height = int(m_height);
            m_layout.orientation = 1;
            m_layout.codec = QStringLiteral("OpenImageIO/png (APNG)");
            m_canvas.assign(frameBytes(m_layout), 0); // fully transparent black
        }
        // The previous frame leaves its region as its dispose operation says.
        if (m_next > 0) {
            const Frame &previous = m_frames[std::size_t(m_next - 1)];
            if (previous.dispose == kDisposeBackground)
                fill(previous, nullptr);
            else if (previous.dispose == kDisposePrevious && !m_saved.empty())
                fill(previous, m_saved.data());
        }
        if (frame.dispose == kDisposePrevious)
            m_saved = m_canvas;
        place(frame, part);

        copyLayout(m_layout, out);
        out->colour = part.colour;
        out->data.reset(new unsigned char[m_canvas.size()]);
        std::memcpy(out->data.get(), m_canvas.data(), m_canvas.size());
        *durationMs = frame.durationMs;
        ++m_next;
        return true;
    }

    bool rewind(QString *) override
    {
        m_next = 0;
        m_saved.clear();
        return true;
    }

    int frameCount() const override { return int(m_frames.size()); }
    int loopCount() const override { return m_plays; }

private:
    static constexpr quint8 kDisposeNone = 0, kDisposeBackground = 1, kDisposePrevious = 2;
    static constexpr quint8 kBlendSource = 0, kBlendOver = 1;

    struct Frame {
        quint32 width = 0, height = 0, x = 0, y = 0;
        int durationMs = 100;
        quint8 dispose = kDisposeNone;
        quint8 blend = kBlendSource;
        QList<QByteArrayView> data; // IDAT or fdAT payloads, in order
    };

    explicit ApngFrames(QByteArray bytes) : m_bytes(std::move(bytes)) {}

    bool parse(QString *error)
    {
        const auto *p = reinterpret_cast<const unsigned char *>(m_bytes.constData());
        const qsizetype size = m_bytes.size();
        if (size < 8 || std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) != 0)
            return false;
        bool animated = false, seenIdat = false;
        quint32 declaredFrames = 0;
        for (qsizetype pos = 8; pos + 12 <= size;) {
            const quint32 length = bigEndian32(p + pos);
            if (length > quint32(size - pos - 12)) { // runs past the end of the file
                *error = damaged("APNG");
                return false;
            }
            const char *type = m_bytes.constData() + pos + 4;
            const unsigned char *data = p + pos + 8;
            const QByteArrayView chunk(m_bytes.constData() + pos, qsizetype(length) + 12);
            pos += qsizetype(length) + 12;
            if (std::memcmp(type, "IHDR", 4) == 0) {
                if (length != 13) {
                    *error = damaged("APNG");
                    return false;
                }
                m_ihdr = QByteArray(reinterpret_cast<const char *>(data), 13);
                m_width = bigEndian32(data);
                m_height = bigEndian32(data + 4);
            } else if (std::memcmp(type, "acTL", 4) == 0 && length == 8 && !seenIdat) {
                animated = true;
                declaredFrames = bigEndian32(data);
                m_plays = int(std::min<quint32>(bigEndian32(data + 4), 1000000));
            } else if (std::memcmp(type, "fcTL", 4) == 0 && animated) {
                Frame frame;
                if (length != 26 || !readControl(data, &frame) || m_frames.size() >= 100000) {
                    *error = damaged("APNG");
                    return false;
                }
                m_frames.push_back(frame);
            } else if (std::memcmp(type, "IDAT", 4) == 0) {
                // The default image is the first frame only when a frame control precedes it.
                if (animated && m_frames.size() == 1) {
                    m_frames.front().data << QByteArrayView(reinterpret_cast<const char *>(data), length);
                    m_firstFromIdat = true;
                }
                seenIdat = true;
            } else if (std::memcmp(type, "fdAT", 4) == 0 && animated) {
                // Frame data needs its frame control first, and the first frame's may come
                // from IDAT only.
                if (length < 4 || m_frames.empty() || (m_frames.size() == 1 && m_firstFromIdat)) {
                    *error = damaged("APNG");
                    return false;
                }
                m_frames.back().data << QByteArrayView(reinterpret_cast<const char *>(data) + 4, length - 4);
            } else if (std::memcmp(type, "IEND", 4) == 0) {
                break;
            } else if (!seenIdat && isColourChunk(type)) {
                m_shared += chunk.toByteArray();
            }
        }
        if (!animated)
            return false;
        if (m_ihdr.isEmpty() || m_width == 0 || m_height == 0 || declaredFrames == 0) {
            *error = damaged("APNG");
            return false;
        }
        for (const Frame &frame : m_frames) {
            if (frame.data.isEmpty() || quint64(frame.x) + frame.width > m_width || quint64(frame.y) + frame.height > m_height) {
                *error = damaged("APNG");
                return false;
            }
        }
        // The specification: the first frame covers the whole canvas.
        if (!m_frames.empty()
            && (m_frames.front().x != 0 || m_frames.front().y != 0 || m_frames.front().width != m_width
                || m_frames.front().height != m_height)) {
            *error = damaged("APNG");
            return false;
        }
        // The first frame covers the canvas and cannot restore what came before it.
        if (!m_frames.empty() && m_frames.front().dispose == kDisposePrevious)
            m_frames.front().dispose = kDisposeBackground;
        return true;
    }

    static bool readControl(const unsigned char *d, Frame *frame)
    {
        frame->width = bigEndian32(d + 4);
        frame->height = bigEndian32(d + 8);
        frame->x = bigEndian32(d + 12);
        frame->y = bigEndian32(d + 16);
        const quint16 numerator = bigEndian16(d + 20), denominator = bigEndian16(d + 22);
        frame->durationMs = playableMs(1000.0 * numerator / (denominator ? denominator : 100));
        frame->dispose = d[24];
        frame->blend = d[25];
        return frame->width > 0 && frame->height > 0 && frame->dispose <= kDisposePrevious && frame->blend <= kBlendOver;
    }

    // Chunks that describe every frame's samples and colour (before the image data).
    static bool isColourChunk(const char *type)
    {
        for (const char *known : {"PLTE", "tRNS", "gAMA", "cHRM", "sRGB", "iCCP", "sBIT", "cICP", "mDCV", "cLLI"})
            if (std::memcmp(type, known, 4) == 0)
                return true;
        return false;
    }

    QByteArray frameAsPng(const Frame &frame) const
    {
        QByteArray png("\x89PNG\r\n\x1a\n", 8);
        QByteArray ihdr = m_ihdr;
        for (int i = 0; i < 4; ++i) {
            ihdr[i] = char(frame.width >> (24 - 8 * i));
            ihdr[4 + i] = char(frame.height >> (24 - 8 * i));
        }
        appendChunk(&png, "IHDR", ihdr);
        png += m_shared;
        for (QByteArrayView part : frame.data)
            appendChunk(&png, "IDAT", part);
        appendChunk(&png, "IEND", {});
        return png;
    }

    // Clears a frame's region to transparent black, or restores it from `from`.
    void fill(const Frame &frame, const unsigned char *from)
    {
        const std::size_t pixel = std::size_t(m_layout.channels * m_layout.sampleBytes());
        for (quint32 row = 0; row < frame.height; ++row) {
            const std::size_t at = ((std::size_t(frame.y) + row) * m_width + frame.x) * pixel;
            if (from)
                std::memcpy(m_canvas.data() + at, from + at, frame.width * pixel);
            else
                std::memset(m_canvas.data() + at, 0, frame.width * pixel);
        }
    }

    template <typename T>
    void blendOver(const T *src, T *dst, quint32 count) const
    {
        constexpr float full = std::is_same_v<T, quint8> ? 255.0f : 65535.0f;
        const int channels = m_layout.channels, alpha = m_layout.alphaIndex;
        for (quint32 i = 0; i < count; ++i, src += channels, dst += channels) {
            const float sa = src[alpha] / full, da = dst[alpha] / full;
            const float a = sa + da * (1.0f - sa);
            for (int c = 0; c < channels; ++c) {
                if (c == alpha)
                    continue;
                const float v = a > 0.0f ? (src[c] * sa + dst[c] * da * (1.0f - sa)) / a : 0.0f;
                dst[c] = T(std::lround(std::clamp(v, 0.0f, full)));
            }
            dst[alpha] = T(std::lround(a * full));
        }
    }

    void place(const Frame &frame, const Decoded &part)
    {
        const std::size_t pixel = std::size_t(m_layout.channels * m_layout.sampleBytes());
        for (quint32 row = 0; row < frame.height; ++row) {
            unsigned char *dst = m_canvas.data() + ((std::size_t(frame.y) + row) * m_width + frame.x) * pixel;
            const unsigned char *src = part.data.get() + std::size_t(row) * frame.width * pixel;
            if (frame.blend == kBlendSource || m_layout.alphaIndex < 0)
                std::memcpy(dst, src, frame.width * pixel);
            else if (m_layout.sample == Decoded::Sample::U8)
                blendOver(src, dst, frame.width);
            else
                blendOver(reinterpret_cast<const quint16 *>(src), reinterpret_cast<quint16 *>(dst), frame.width);
        }
    }

    QByteArray m_bytes;    // the file; the frames' data are views into it
    QByteArray m_ihdr;     // the 13 bytes of the header's data
    QByteArray m_shared;   // whole colour chunks, copied into every frame
    quint32 m_width = 0, m_height = 0;
    int m_plays = 0;
    std::vector<Frame> m_frames;
    bool m_firstFromIdat = false; // the first frame's data is the default image (IDAT)
    Decoded m_layout;      // the canvas: the first frame's samples, the file's size
    std::vector<unsigned char> m_canvas;
    std::vector<unsigned char> m_saved; // before a frame whose region is restored afterwards
    int m_next = 0;
};

} // namespace

bool codecAvailable(Decoder decoder)
{
    switch (decoder) {
    case Decoder::Jxl:
#ifdef IMAGEVIEWER_HAVE_LIBJXL
        return true;
#else
        return false;
#endif
    case Decoder::WebP:
#ifdef IMAGEVIEWER_HAVE_LIBWEBP
        return true;
#else
        return false;
#endif
    case Decoder::Apng: return true;
    case Decoder::OpenImageIO:
    case Decoder::Heif:
    case Decoder::Qt:
    case Decoder::GraphicsMagick:
    case Decoder::System: break;
    }
    return false;
}

bool decodeCodec(Decoder decoder, QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                 std::unique_ptr<FrameReader> *frames)
{
    switch (decoder) {
#ifdef IMAGEVIEWER_HAVE_LIBJXL
    case Decoder::Jxl: {
        std::unique_ptr<JxlFrames> reader = JxlFrames::open(std::move(bytes), maxPixels, out, error);
        if (!reader)
            return false;
        if (frames && reader->isAnimated())
            *frames = std::move(reader);
        return true;
    }
#endif
#ifdef IMAGEVIEWER_HAVE_LIBWEBP
    case Decoder::WebP: {
        std::unique_ptr<WebPFrames> reader = WebPFrames::open(std::move(bytes), maxPixels, out, error);
        if (!reader)
            return false;
        if (frames && reader->isAnimated())
            *frames = std::move(reader);
        return true;
    }
#endif
    case Decoder::Apng: {
        QByteArray copy = bytes; // shared, not copied: a plain PNG is read from it below
        if (std::unique_ptr<ApngFrames> reader = ApngFrames::open(std::move(bytes), maxPixels, out, error)) {
            if (frames)
                *frames = std::move(reader);
            return true;
        }
        if (!error->isEmpty() || out->overLimit)
            return false;
        *out = Decoded(); // no animation control: the default image, as any PNG
        return decodeOiioMemory(copy, "image.png", out, error);
    }
    default: break;
    }
    *error = damaged("?");
    return false;
}
