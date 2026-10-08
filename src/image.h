// Decoding front end: file -> linear scRGB half-float RGBA (premultiplied). The format
// registry picks the back end (formats.h, decoders.h); the colour conversion happens here,
// once, for every back end.
#pragma once

#include "color.h"

#include <QDateTime>
#include <QFloat16>
#include <QImage>
#include <QLocale>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// Shooting data from the file's EXIF, when the decoder exposes it (strings already cleaned:
// no control characters, at most 64 characters).
struct CameraInfo {
    QString make;
    QString model;
    QString lens;
    QDateTime taken;          // DateTimeOriginal, local time of the camera
    float exposureTime = 0.0f; // seconds
    float fNumber = 0.0f;
    float focalLength = 0.0f;  // millimetres
    int iso = 0;

    bool isEmpty() const
    {
        return make.isEmpty() && model.isEmpty() && lens.isEmpty() && !taken.isValid() && exposureTime <= 0.0f
               && fNumber <= 0.0f && focalLength <= 0.0f && iso <= 0;
    }
};

using PixelBuffer = std::shared_ptr<const std::vector<qfloat16>>;

class FrameReader;
class Animation;

struct Image {
    QString path;
    QString error;   // non-empty when decoding failed
    bool overPixelLimit = false; // not decoded: the source exceeds the pixel limit that was asked for
    qint64 fileSize = -1;   // bytes, read before decoding
    QDateTime modified;     // of the file, read before decoding
    QString codec;   // backend/format, e.g. "OpenImageIO/jpeg"
    int width = 0;
    int height = 0;
    int sourceWidth = 0; // before any downscale to the GPU texture limit
    int sourceHeight = 0;
    int sourceChannels = 0;
    int sourceBits = 0; // bits per channel of the stored data
    bool sourceFloat = false;
    bool hasAlpha = false;
    int orientation = 1; // EXIF orientation that was applied (1 = none)
    color::Descriptor colour;
    float maxComponent = 0.0f; // brightest RGB component in working units (1.0 = SDR white)
    float maxLuminance = 0.0f; // brightest luminance (BT.709 Y) in working units
    CameraInfo camera;
    double decodeMs = 0.0;
    // width * height * 4, linear scRGB, premultiplied alpha. Shared, never modified: the
    // preload cache and the renderer hold the same buffer (decision D-33). The first frame
    // of an animation.
    PixelBuffer pixels;
    // The frames after the first, for an animated image (E6); null for still images.
    std::shared_ptr<Animation> animation;

    bool isValid() const { return error.isEmpty() && pixels && !pixels->empty(); }
    qint64 pixelBytes() const { return pixels ? qint64(pixels->size() * sizeof(qfloat16)) : 0; }
};

// The frames of an animated image (E6), decoded on demand and converted exactly like the
// first (same colour conversion, orientation and size). Frames stay in memory while they fit
// in a budget, so later loops and stepping back decode nothing. frame() may run on a worker
// thread; one call at a time is served, the others wait.
class Animation {
public:
    struct Frame {
        int index = 0;
        PixelBuffer pixels;  // as Image::pixels
        int durationMs = 0;  // how long it is shown
        float maxComponent = 0.0f; // as Image::maxComponent, for this frame
        float maxLuminance = 0.0f;
    };

    Animation(std::unique_ptr<FrameReader> reader, std::unique_ptr<color::Converter> converter, int factor,
              int width, int height, PixelBuffer first, int firstDurationMs);
    ~Animation();
    Animation(const Animation &) = delete;
    Animation &operator=(const Animation &) = delete;

    int frameCount() const { return m_count.load(); } // 0 while unknown
    int loopCount() const { return m_loops; }         // 0 = forever
    int firstDurationMs() const { return m_firstDurationMs; }
    // Frame `index`; the first frame when `index` lies past the last one (whose number is
    // known from then on). False with *error set when the file cannot be read any more.
    // Runs on a worker thread and never throws (out of memory is an error like any other).
    bool frame(int index, Frame *out, QString *error);
    // Drops every kept frame but the first, for an animation no longer on screen: the preload
    // cache, which may keep it, counts the first frame only. Waits for a frame() in progress.
    void trim();

private:
    bool frameLocked(int index, Frame *out, QString *error);
    std::mutex m_mutex;
    std::unique_ptr<FrameReader> m_reader;
    std::unique_ptr<color::Converter> m_converter;
    int m_factor = 1;
    int m_width = 0;
    int m_height = 0;
    int m_loops = 0;
    int m_firstDurationMs = 0;
    qint64 m_budget = 0;
    std::atomic<int> m_count{0};
    int m_next = 1;               // index of the frame the reader produces next
    std::vector<Frame> m_kept;    // by index; the first is always there
    qint64 m_keptBytes = 0;
    bool m_keepAll = true;        // every frame fits in the budget so far
};

// Decodes `path`. Images larger than `maxTextureSize` on either axis are reduced
// with a box filter in linear light (tiling arrives in Phase 2). With `maxPixels` > 0,
// a source of more pixels is not decoded (overPixelLimit), judged from the header.
Image decodeImage(const QString &path, int maxTextureSize, qint64 maxPixels = 0);

// Decodes `path` at full resolution for the clipboard: 16-bit sRGB, straight alpha.
// HDR values above SDR white and colours outside sRGB are clipped (the clipboard
// formats other applications read are SDR). A null image when decoding fails.
QImage decodeForClipboard(const QString &path);

// Stops the decoders' own worker threads. Called once, after the last decode has finished
// and before the process exits: OpenImageIO requires it, and threads left to the C++
// runtime's exit-time destructors can abort the process (seen on macOS).
void shutdownDecoders();

// An exposure time as photographers write it, without the unit: "1/250" below one second
// (the reciprocal rounded), "2.5" from one second up.
QString exposureTimeText(float seconds, const QLocale &locale);

// Installed physical memory in bytes, 0 when unknown.
qint64 physicalMemoryBytes();
