// Decoding front end: file -> linear scRGB half-float RGBA (premultiplied).
// Backends are tried in order (OpenImageIO, then Qt's QImageReader); FFmpeg and
// SVG backends join in Phase 2 (docs/PLAN.md §9).
#pragma once

#include "color.h"

#include <QDateTime>
#include <QFloat16>
#include <QImage>
#include <QLocale>
#include <QString>
#include <QStringList>

#include <memory>
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
    // preload cache and the renderer hold the same buffer (decision D-33).
    PixelBuffer pixels;

    bool isValid() const { return error.isEmpty() && pixels && !pixels->empty(); }
    qint64 pixelBytes() const { return pixels ? qint64(pixels->size() * sizeof(qfloat16)) : 0; }
};

// Decodes `path`. Images larger than `maxTextureSize` on either axis are reduced
// with a box filter in linear light (tiling arrives in Phase 2). With `maxPixels` > 0,
// a source of more pixels is not decoded (overPixelLimit), judged from the header.
Image decodeImage(const QString &path, int maxTextureSize, qint64 maxPixels = 0);

// Decodes `path` at full resolution for the clipboard: 16-bit sRGB, straight alpha.
// HDR values above SDR white and colours outside sRGB are clipped (the clipboard
// formats other applications read are SDR). A null image when decoding fails.
QImage decodeForClipboard(const QString &path);

// Lower-case file suffixes (without dot) that the available backends can read.
const QStringList &supportedSuffixes();

// An exposure time as photographers write it, without the unit: "1/250" below one second
// (the reciprocal rounded), "2.5" from one second up.
QString exposureTimeText(float seconds, const QLocale &locale);

// Installed physical memory in bytes, 0 when unknown.
qint64 physicalMemoryBytes();
