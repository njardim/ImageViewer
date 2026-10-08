// AVIF image sequences through libheif's track API (libheif 1.23 or later; decision D-38).
// Still AVIF images go through OpenImageIO; this reader plays the files whose "ftyp" box
// names an image sequence ("avis"), as browsers do when a file has both.
#include "decoders.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstring>
#include <memory>

#ifdef IMAGEVIEWER_HAVE_LIBHEIF
#include <libheif/heif.h>
#ifdef LIBHEIF_HAVE_VERSION
#if LIBHEIF_HAVE_VERSION(1, 23, 0) // sequences, repetitions, nclx passthrough
#define IMAGEVIEWER_HEIF_SEQUENCES
#include <libheif/heif_sequences.h>
#endif
#endif
#endif

namespace {

#ifdef IMAGEVIEWER_HEIF_SEQUENCES
using color::Descriptor;

// The colour of decoded RGB samples from an nclx box or AV1 colour description (D-22).
bool describeNclx(const heif_color_profile_nclx &nclx, Descriptor *d)
{
    Descriptor c;
    if (!color::primariesFromCicp(int(nclx.color_primaries), &c.primaries)
        || !color::transferFromCicp(int(nclx.transfer_characteristics), &c.transfer, &c.gamma))
        return false;
    d->source = Descriptor::Source::Cicp;
    d->primaries = c.primaries;
    d->transfer = c.transfer;
    d->gamma = c.gamma;
    d->fullRange = true; // libheif expands narrow-range YCbCr into full-range RGB
    d->description = QStringLiteral("CICP %1/%2/%3/%4 — %5")
                         .arg(int(nclx.color_primaries))
                         .arg(int(nclx.transfer_characteristics))
                         .arg(int(nclx.matrix_coefficients))
                         .arg(nclx.full_range_flag ? 1 : 0)
                         .arg(color::transferName(c.transfer, c.gamma));
    return true;
}

// CICP first, then an ICC profile; false when the image carries neither.
bool describeImage(const heif_image *image, Descriptor *d)
{
    heif_color_profile_nclx *nclx = nullptr;
    if (heif_image_get_color_profile_type(image) == heif_color_profile_type_nclx
        && heif_image_get_nclx_color_profile(image, &nclx).code == heif_error_Ok && nclx) {
        const bool known = describeNclx(*nclx, d);
        heif_nclx_color_profile_free(nclx);
        if (known)
            return true;
    }
    const std::size_t size = heif_image_get_raw_color_profile_size(image);
    if (size > 0 && size < (std::size_t(1) << 26)) {
        QByteArray icc(qsizetype(size), Qt::Uninitialized);
        if (heif_image_get_raw_color_profile(image, icc.data()).code == heif_error_Ok) {
            describeIcc(icc, d);
            return true;
        }
    }
    return false;
}

// The same from the file's still image, which encoders write next to the sequence.
bool describeHandle(heif_context *context, Descriptor *d)
{
    heif_image_handle *handle = nullptr;
    if (heif_context_get_primary_image_handle(context, &handle).code != heif_error_Ok || !handle)
        return false;
    bool known = false;
    heif_color_profile_nclx *nclx = nullptr;
    if (heif_image_handle_get_nclx_color_profile(handle, &nclx).code == heif_error_Ok && nclx) {
        known = describeNclx(*nclx, d);
        heif_nclx_color_profile_free(nclx);
    }
    const std::size_t size = known ? 0 : heif_image_handle_get_raw_color_profile_size(handle);
    if (size > 0 && size < (std::size_t(1) << 26)) {
        QByteArray icc(qsizetype(size), Qt::Uninitialized);
        if (heif_image_handle_get_raw_color_profile(handle, icc.data()).code == heif_error_Ok) {
            describeIcc(icc, d);
            known = true;
        }
    }
    heif_image_handle_release(handle);
    return known;
}

using ImagePtr = std::unique_ptr<heif_image, decltype(&heif_image_release)>;

// The frames of an AVIF sequence at their native depth (8 bits, or 10 and 12 widened to
// 16), straight alpha unless the file says otherwise. libheif plays the media once
// (its edit list ignored); the Animation class loops.
class HeifFrames final : public FrameReader {
public:
    static std::unique_ptr<HeifFrames> open(QByteArray bytes, qint64 maxPixels, Decoded *first, QString *error)
    {
        std::unique_ptr<HeifFrames> reader(new HeifFrames(std::move(bytes)));
        if (!reader->start(maxPixels, first, error))
            return nullptr;
        if (!reader->next(first, &first->durationMs, error)) {
            if (error->isEmpty())
                *error = damaged("AVIF");
            return nullptr;
        }
        return reader;
    }

    bool next(Decoded *out, int *durationMs, QString *error) override
    {
        heif_image *raw = nullptr;
        const heif_error result = heif_track_decode_next_image(m_track.get(), &raw, heif_colorspace_RGB,
                                                               m_wide ? heif_chroma_interleaved_RRGGBBAA_LE
                                                                      : heif_chroma_interleaved_RGBA,
                                                               m_options.get());
        ImagePtr image(raw, heif_image_release);
        if (result.code == heif_error_End_of_sequence) {
            m_count = m_index;
            return false;
        }
        size_t stride = 0;
        const uint8_t *plane = image && result.code == heif_error_Ok
                                   ? heif_image_get_plane_readonly2(image.get(), heif_channel_interleaved, &stride)
                                   : nullptr;
        const int bits = image ? heif_image_get_bits_per_pixel_range(image.get(), heif_channel_interleaved) : 0;
        if (!plane || heif_image_get_primary_width(image.get()) != m_layout.width
            || heif_image_get_primary_height(image.get()) != m_layout.height || bits <= 0 || bits > 16
            || stride < std::size_t(m_layout.width) * 4 * std::size_t(m_layout.sampleBytes())) {
            *error = damaged("AVIF");
            return false;
        }
        copyLayout(m_layout, out);
        out->associatedAlpha = m_layout.alphaIndex >= 0 && heif_image_is_premultiplied_alpha(image.get());
        const std::size_t rowBytes = std::size_t(m_layout.width) * 4 * std::size_t(m_layout.sampleBytes());
        out->data.reset(new unsigned char[rowBytes * std::size_t(m_layout.height)]);
        for (int y = 0; y < m_layout.height; ++y) {
            const uint8_t *from = plane + std::size_t(y) * stride;
            unsigned char *to = out->data.get() + std::size_t(y) * rowBytes;
            if (!m_wide) {
                std::memcpy(to, from, rowBytes);
                continue;
            }
            // 10 or 12 significant bits, little endian, widened to the full 16-bit range.
            const quint32 max = (quint32(1) << bits) - 1;
            for (std::size_t i = 0; i < rowBytes; i += 2) {
                const quint32 v = std::min<quint32>(quint32(from[i]) | quint32(from[i + 1]) << 8, max);
                const quint16 wide = quint16((v * 65535u + max / 2) / max);
                std::memcpy(to + i, &wide, 2);
            }
        }
        const quint64 ticks = heif_image_get_duration(image.get());
        *durationMs = playableMs(double(ticks) * 1000.0 / m_timescale);
        ++m_index;
        return true;
    }

    bool rewind(QString *error) override
    {
        // libheif has no way back to the first sample: open the track again.
        if (!openTrack()) {
            *error = damaged("AVIF");
            return false;
        }
        m_index = 0;
        return true;
    }

    int frameCount() const override { return m_count; }
    int loopCount() const override { return m_loops; }

private:
    explicit HeifFrames(QByteArray bytes)
        : m_bytes(std::move(bytes))
        , m_context(nullptr, heif_context_free)
        , m_track(nullptr, heif_track_release)
        , m_options(heif_decoding_options_alloc(), heif_decoding_options_free)
    {
    }

    bool openTrack()
    {
        m_track.reset();
        m_context.reset(heif_context_alloc());
        if (!m_context
            || heif_context_read_from_memory_without_copy(m_context.get(), m_bytes.constData(), std::size_t(m_bytes.size()),
                                                          nullptr)
                       .code
                   != heif_error_Ok
            || !heif_context_has_sequence(m_context.get()))
            return false;
        m_track.reset(heif_context_get_track(m_context.get(), 0)); // the first visual track
        return m_track != nullptr;
    }

    bool start(qint64 maxPixels, Decoded *first, QString *error)
    {
        const QString invalid = damaged("AVIF");
        if (!m_options || !openTrack()) {
            *error = invalid;
            return false;
        }
        m_options->ignore_sequence_editlist = 1;            // one pass; loops are ours
        m_options->output_image_nclx_profile_passthrough = 1; // keep the file's colour, not sRGB
        uint16_t w = 0, h = 0;
        m_timescale = heif_track_get_timescale(m_track.get());
        if (heif_track_get_image_resolution(m_track.get(), &w, &h).code != heif_error_Ok || m_timescale == 0) {
            *error = invalid;
            return false;
        }
        const qint64 pixels = qint64(w) * h;
        if (w == 0 || h == 0 || pixels > kMaxPixels) {
            *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(w).arg(h).arg(4);
            return false;
        }
        if (maxPixels > 0 && pixels > maxPixels) {
            first->overLimit = true;
            return false;
        }
        if (!fitsInMemory(pixels, 8, error))
            return false;
        // The depth of the coded samples, from the first frame as coded: asking libheif for
        // 16-bit RGB would turn an 8-bit file into 10-bit samples.
        heif_image *raw = nullptr;
        const heif_error coded = heif_track_decode_next_image(m_track.get(), &raw, heif_colorspace_undefined,
                                                              heif_chroma_undefined, m_options.get());
        ImagePtr image(raw, heif_image_release);
        if (coded.code != heif_error_Ok || !image) {
            *error = invalid;
            return false;
        }
        const heif_channel channel = heif_image_get_colorspace(image.get()) == heif_colorspace_RGB
                                             && heif_image_has_channel(image.get(), heif_channel_interleaved)
                                         ? heif_channel_interleaved
                                     : heif_image_get_colorspace(image.get()) == heif_colorspace_RGB ? heif_channel_R
                                                                                                      : heif_channel_Y;
        const int bits = heif_image_get_bits_per_pixel_range(image.get(), channel);
        if (bits <= 0 || bits > 16) {
            *error = invalid;
            return false;
        }
        if (!describeImage(image.get(), &first->colour) && !describeHandle(m_context.get(), &first->colour))
            assumeDefault(false, &first->colour);
        image.reset();
        const bool alpha = heif_track_has_alpha_channel(m_track.get()) != 0;
        // Files without an edit list read as one pass, which is also how libheif reports an
        // explicit single pass; like browsers, both loop forever. Unknown patterns too.
        const uint32_t repetitions = heif_track_get_number_of_repetitions(m_track.get());
        m_loops = repetitions <= 1 || repetitions == heif_sequence_track_number_of_repetitions_infinite
                      ? 0
                      : int(std::min<uint32_t>(repetitions, 65535));
        if (!openTrack()) { // back before the first frame
            *error = invalid;
            return false;
        }
        m_wide = bits > 8;
        first->width = w;
        first->height = h;
        first->sample = m_wide ? Decoded::Sample::U16 : Decoded::Sample::U8;
        first->channels = 4;
        first->alphaIndex = alpha ? 3 : -1;
        first->sourceChannels = alpha ? 4 : 3;
        first->bits = bits;
        first->codec = QStringLiteral("libheif (sequence)");
        copyLayout(*first, &m_layout);
        return true;
    }

    QByteArray m_bytes; // libheif reads from it without copying
    std::unique_ptr<heif_context, decltype(&heif_context_free)> m_context;
    std::unique_ptr<heif_track, decltype(&heif_track_release)> m_track;
    std::unique_ptr<heif_decoding_options, decltype(&heif_decoding_options_free)> m_options;
    Decoded m_layout;
    uint32_t m_timescale = 1;
    bool m_wide = false;
    int m_index = 0;
    int m_count = 0;
    int m_loops = 0;
};
#endif // IMAGEVIEWER_HEIF_SEQUENCES

} // namespace

bool heifSequencesAvailable()
{
#ifdef IMAGEVIEWER_HEIF_SEQUENCES
    return true;
#else
    return false;
#endif
}

bool decodeHeifSequence(QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error, std::unique_ptr<FrameReader> *frames)
{
#ifdef IMAGEVIEWER_HEIF_SEQUENCES
    std::unique_ptr<HeifFrames> reader = HeifFrames::open(std::move(bytes), maxPixels, out, error);
    if (!reader)
        return false;
    if (frames)
        *frames = std::move(reader);
    return true;
#else
    Q_UNUSED(bytes);
    Q_UNUSED(maxPixels);
    Q_UNUSED(out);
    Q_UNUSED(frames);
    *error = damaged("AVIF");
    return false;
#endif
}
