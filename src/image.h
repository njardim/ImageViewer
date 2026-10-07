// Decoding front end: file -> linear scRGB half-float RGBA (premultiplied).
// Backends are tried in order (OpenImageIO, then Qt's QImageReader); FFmpeg and
// SVG backends join in Phase 2 (docs/PLAN.md §9).
#pragma once

#include "color.h"

#include <QFloat16>
#include <QImage>
#include <QString>
#include <QStringList>

#include <vector>

struct Image {
    QString path;
    QString error;   // non-empty when decoding failed
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
    double decodeMs = 0.0;
    std::vector<qfloat16> pixels; // width * height * 4, linear scRGB, premultiplied alpha

    bool isValid() const { return error.isEmpty() && !pixels.empty(); }
};

// Decodes `path`. Images larger than `maxTextureSize` on either axis are reduced
// with a box filter in linear light (tiling arrives in Phase 2).
Image decodeImage(const QString &path, int maxTextureSize);

// Decodes `path` at full resolution for the clipboard: 16-bit sRGB, straight alpha.
// HDR values above SDR white and colours outside sRGB are clipped (the clipboard
// formats other applications read are SDR). A null image when decoding fails.
QImage decodeForClipboard(const QString &path);

// Lower-case file suffixes (without dot) that the available backends can read.
const QStringList &supportedSuffixes();
