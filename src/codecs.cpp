// Back ends that parse the file from memory (decision D-38): libjxl (JPEG XL), libwebp (WebP,
// its ICC profile and animation) and our Softimage PIC reader (D-45); APNG is in apng.cpp.
// Shared helpers are in decoders.cpp.
#include "decoders.h"

#include <QCoreApplication>

#include <algorithm>
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

// Softimage PIC (D-45), read here because OpenImageIO's reader crashes on a truncated file. A
// 104-byte header (big-endian), then channel packets of 4 bytes (another packet follows, bits
// per sample, encoding, channel mask R 0x80 G 0x40 B 0x20 A 0x10), then the scanlines from the
// top, each holding every packet's channels in packet order. Encodings: 0 raw; 1 runs of a
// count (1-255) and one pixel; 2 mixed, where a count byte c < 128 is followed by c + 1 raw
// pixels, 128 by a 16-bit count and one pixel, and c > 128 by one pixel repeated c - 127 times.
// Every read is checked against the file; a run may not leave its scanline.
bool decodeSoftimage(const QByteArray &bytes, qint64 maxPixels, Decoded *out, QString *error)
{
    const QString invalid = damaged("Softimage PIC");
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData());
    const qsizetype size = bytes.size();
    constexpr qsizetype kHeaderBytes = 104;
    if (size < kHeaderBytes + 4 || std::memcmp(p, "\x53\x80\xF6\x34", 4) != 0 || std::memcmp(p + 88, "PICT", 4) != 0) {
        *error = invalid;
        return false;
    }
    const int w = p[92] << 8 | p[93];
    const int h = p[94] << 8 | p[95];
    if (w <= 0 || h <= 0) {
        *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(w).arg(h).arg(4);
        return false;
    }
    struct Packet {
        int encoding;
        int channels[4]; // destination channels, R G B A order
        int count;
    };
    std::vector<Packet> packets;
    bool alpha = false;
    qsizetype at = kHeaderBytes;
    for (bool more = true; more;) {
        if (at + 4 > size || packets.size() >= 8) {
            *error = invalid;
            return false;
        }
        const unsigned char *q = p + at;
        at += 4;
        more = q[0] != 0;
        Packet packet{q[2], {}, 0};
        for (int c = 0; c < 4; ++c)
            if (q[3] & (0x80 >> c))
                packet.channels[packet.count++] = c;
        if (q[1] != 8 || packet.encoding > 2 || packet.count == 0 || (q[3] & 0x0F)) {
            *error = invalid; // 8 bits per sample is all the format's writers produce
            return false;
        }
        alpha = alpha || (q[3] & 0x10);
        packets.push_back(packet);
    }
    if (maxPixels > 0 && qint64(w) * h > maxPixels) {
        out->overLimit = true;
        return false;
    }
    if (!fitsInMemory(qint64(w) * h, 4, error))
        return false;

    const int channels = alpha ? 4 : 3;
    const std::size_t rowBytes = std::size_t(w) * channels;
    out->data.reset(new unsigned char[rowBytes * h]);
    std::memset(out->data.get(), 0, rowBytes * h); // a colour channel no packet holds stays 0
    for (int y = 0; y < h; ++y) {
        unsigned char *row = out->data.get() + rowBytes * y;
        for (const Packet &packet : packets) {
            const auto pixelAt = [&](int x, const unsigned char *src) {
                for (int i = 0; i < packet.count; ++i)
                    row[std::size_t(x) * channels + packet.channels[i]] = src[i];
            };
            int x = 0;
            while (x < w) {
                int run = 0;
                bool raw = packet.encoding == 0;
                if (raw) {
                    run = w;
                } else {
                    if (at >= size) {
                        *error = invalid;
                        return false;
                    }
                    const int c = p[at++];
                    if (packet.encoding == 1) {
                        run = c;
                    } else if (c < 128) {
                        run = c + 1;
                        raw = true;
                    } else if (c == 128) {
                        if (at + 2 > size) {
                            *error = invalid;
                            return false;
                        }
                        run = p[at] << 8 | p[at + 1];
                        at += 2;
                    } else {
                        run = c - 127;
                    }
                }
                const qsizetype need = qsizetype(raw ? run : 1) * packet.count;
                if (run <= 0 || run > w - x || need > size - at) {
                    *error = invalid;
                    return false;
                }
                for (int i = 0; i < run; ++i)
                    pixelAt(x + i, p + at + (raw ? qsizetype(i) * packet.count : 0));
                at += need;
                x += run;
            }
        }
    }
    out->width = w;
    out->height = h;
    out->sample = Decoded::Sample::U8;
    out->channels = channels;
    out->sourceChannels = channels;
    out->alphaIndex = alpha ? 3 : -1;
    out->bits = 8;
    out->codec = QStringLiteral("imageViewer (Softimage PIC)");
    assumeDefault(false, &out->colour);
    return true;
}

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
    case Decoder::Apng:
    case Decoder::Softimage: return true;
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
    case Decoder::Apng: return decodeApng(std::move(bytes), maxPixels, out, error, frames);
    case Decoder::Softimage: return decodeSoftimage(bytes, maxPixels, out, error);
    default: break;
    }
    *error = damaged("?");
    return false;
}
