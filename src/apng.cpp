// Animated PNG (decisions D-38, D-40): each frame is rebuilt as a plain PNG, decoded by
// OpenImageIO and composited as the APNG specification says. Split from codecs.cpp (D-41).
#include "decoders.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

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

bool decodeApng(QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                std::unique_ptr<FrameReader> *frames)
{
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
