#include "histogram.h"

#include <QFloat16>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace {

using color::Transfer;

quint16 halfBits(float v)
{
    const qfloat16 h(v);
    quint16 bits;
    std::memcpy(&bits, &h, sizeof bits);
    return bits;
}

int binOf(double signal)
{
    // Below the axis, or NaN, the first bin; past it the last. Never rounded first: std::lround of a
    // value no long holds (infinity, a file's 1e30) is unspecified, and where a long has 32 bits
    // (Windows) it gave a negative bin.
    if (!(signal > 0.0))
        return 0;
    if (signal >= 1.0)
        return Histogram::kBins - 1;
    return int(std::lround(signal * (Histogram::kBins - 1)));
}

} // namespace

void Histogram::add(const Histogram &other)
{
    pixels += other.pixels;
    for (std::size_t c = 0; c < bins.size(); ++c)
        for (std::size_t i = 0; i < bins[c].size(); ++i)
            bins[c][i] += other.bins[c][i];
}

HistogramCounter::HistogramCounter(const color::Descriptor &colour, bool cmyk, int containerBits)
{
    // The colours from the file's own samples, unless they are ink, or linear light (a linear axis
    // crowds the shadows into a few bins).
    const bool analytic = colour.source != color::Descriptor::Source::Icc;
    m_native = !cmyk && !(analytic && colour.transfer == Transfer::Linear);
    if (containerBits > 0) {
        m_maxCode = double((1 << containerBits) - 1);
        const double k = double(1 << (containerBits - 8));
        m_blackCode = 16.0 * k;
        m_codeRange = 219.0 * k;
        m_narrow = m_native && analytic && !colour.fullRange;
    }
    // The axis the luminance is counted on, and the colours without the file's samples.
    std::function<float(float)> encode;
    float white = 1.0f;
    if (m_native) {
        m_encoder = std::make_unique<color::Encoder>(colour, false);
        if (m_encoder->valid()) {
            m_separable = m_encoder->separable();
            // A grey keeps its luminance in any primaries of the same white: the green channel's curve.
            encode = [this](float y) { return m_encoder->encodeChannel(1, y); };
            float w[3] = {1.0f, 1.0f, 1.0f};
            m_encoder->encode(w);
            white = (w[0] + w[1] + w[2]) / 3.0f;
            m_axis = analytic ? color::transferName(colour.transfer, colour.gamma) : QStringLiteral("ICC");
        } else { // a profile with a LUT: its colours as stored, the luminance on the sRGB curve
            m_encoder.reset();
            encode = [](float y) { return color::linearToSrgb(y); };
            m_axis = QStringLiteral("ICC");
        }
    } else if (containerBits == 0) { // linear floats: they may hold HDR
        encode = [](float v) { return color::nitsToPq(std::max(v, 0.0f) * color::kSdrReferenceWhiteNits); };
        white = color::nitsToPq(color::kSdrReferenceWhiteNits);
        m_axis = QStringLiteral("PQ");
    } else {
        encode = [](float v) { return color::linearToSrgb(v); };
        m_axis = QStringLiteral("sRGB");
    }
    m_referenceWhite = white > 0.98f ? -1.0 : double(white);
    if (!m_separable)
        return;
    // A table from each half float to its bin: the luminance's, and the colours' on the display axis.
    m_tables.resize(65536);
    for (int bits = 0; bits < 65536; ++bits) {
        qfloat16 h;
        const quint16 b = quint16(bits);
        std::memcpy(static_cast<void *>(&h), &b, sizeof b);
        const float v = float(h);
        m_tables[std::size_t(bits)] = quint8(std::isnan(v) ? 0 : std::isinf(v) ? (v > 0 ? Histogram::kBins - 1 : 0) : binOf(encode(v)));
    }
}

Histogram HistogramCounter::empty() const
{
    Histogram h;
    h.axis = m_axis;
    h.referenceWhite = m_referenceWhite;
    return h;
}

int HistogramCounter::lookUp(float v) const
{
    return m_tables[halfBits(v)];
}

void HistogramCounter::countSignal(const float *rgba, std::size_t pixels, Histogram *into) const
{
    if (!m_native)
        return;
    for (const float *p = rgba, *end = rgba + pixels * 4; p < end; p += 4) {
        if (!(p[3] > 0.0f))
            continue; // no colour
        for (int c = 0; c < 3; ++c) {
            double s = p[c];
            if (m_narrow) // codes 16 to 235 (at 8 bits) are black to white
                s = (s * m_maxCode - m_blackCode) / m_codeRange;
            ++into->bins[std::size_t(c)][std::size_t(binOf(s))];
        }
    }
}

void HistogramCounter::countLinear(const float *rgba, std::size_t pixels, Histogram *into) const
{
    for (const float *p = rgba, *end = rgba + pixels * 4; p < end; p += 4) {
        if (!(p[3] > 0.0f))
            continue;
        ++into->pixels;
        const float y = color::luminance(p[0], p[1], p[2]);
        if (m_separable) {
            ++into->bins[Histogram::Luminance][std::size_t(lookUp(y))];
        } else { // HLG: its OOTF mixes the channels
            float grey[3] = {y, y, y};
            m_encoder->encode(grey);
            ++into->bins[Histogram::Luminance][std::size_t(binOf(grey[1]))];
        }
        if (!m_native)
            for (int c = 0; c < 3; ++c)
                ++into->bins[std::size_t(c)][std::size_t(lookUp(p[c]))];
    }
}

void drawHistogram(QPainter &painter, const QRectF &rect, const Histogram &histogram, bool colors, const QColor &ink,
                   const QColor &plot)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(plot);
    painter.drawRoundedRect(rect, 4, 4);
    if (histogram.isEmpty()) {
        painter.restore();
        return;
    }
    const QRectF area = rect.adjusted(2, 4, -2, -2);
    const QList<int> channels = colors ? QList<int>{Histogram::Red, Histogram::Green, Histogram::Blue}
                                       : QList<int>{Histogram::Luminance};
    // The scale: the tallest bin inside the axis; the ends (clipped shadows and highlights) may be cut.
    quint32 tallest = 0, tallestAnywhere = 0;
    for (const int c : channels) {
        const auto &bins = histogram.bins[std::size_t(c)];
        tallest = std::max(tallest, *std::max_element(bins.cbegin() + 1, bins.cend() - 1));
        tallestAnywhere = std::max(tallestAnywhere, *std::max_element(bins.cbegin(), bins.cend()));
    }
    if (tallest == 0)
        tallest = tallestAnywhere;
    const double step = area.width() / Histogram::kBins;
    const auto shape = [&](int channel) {
        QPainterPath path(QPointF(area.left(), area.bottom()));
        const auto &bins = histogram.bins[std::size_t(channel)];
        for (int i = 0; i < Histogram::kBins; ++i) {
            const double h = std::min(1.0, double(bins[std::size_t(i)]) / tallest) * area.height();
            path.lineTo(area.left() + (i + 0.5) * step, area.bottom() - h);
        }
        path.lineTo(area.right(), area.bottom());
        path.closeSubpath();
        return path;
    };
    // The red, green and blue of the reference, brightened a little to read on the dark plot.
    const QColor hues[3] = {QColor(255, 72, 72), QColor(72, 220, 96), QColor(72, 140, 255)};
    painter.setClipRect(rect);
    if (colors) {
        painter.setCompositionMode(QPainter::CompositionMode_Plus); // where the three meet, grey
        for (const int c : channels) {
            QColor fill = hues[c];
            fill.setAlphaF(0.42f * ink.alphaF());
            painter.setBrush(fill);
            painter.drawPath(shape(c));
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setBrush(Qt::NoBrush);
        for (const int c : channels) {
            QColor line = hues[c];
            line.setAlphaF(0.9f * ink.alphaF());
            painter.setPen(QPen(line, 1.0));
            painter.drawPath(shape(c));
        }
    } else {
        QColor fill = ink;
        fill.setAlphaF(0.35f * ink.alphaF());
        painter.setBrush(fill);
        painter.setPen(QPen(ink, 1.0));
        painter.drawPath(shape(Histogram::Luminance));
    }
    if (histogram.referenceWhite > 0.0) { // SDR white on an HDR axis
        QColor mark = ink;
        mark.setAlphaF(0.5f * ink.alphaF());
        painter.setPen(QPen(mark, 1.0, Qt::DotLine));
        const double x = std::round(area.left() + histogram.referenceWhite * area.width()) + 0.5;
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    painter.restore();
}
