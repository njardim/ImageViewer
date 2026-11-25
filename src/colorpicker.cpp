#include "colorpicker.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

// A number as CSS writes it: a point, at most `decimals` of them, no trailing zeros.
QString css(double v, int decimals)
{
    QString text = QString::number(v, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    if (text == QLatin1String("-0"))
        text = QStringLiteral("0");
    return text;
}

QString hex2(int v)
{
    return QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
}

// Hue in degrees, from sRGB-encoded components in [0, 1].
double hue(double r, double g, double b)
{
    const double mx = std::max({r, g, b}), d = mx - std::min({r, g, b});
    if (d <= 0)
        return 0;
    double h = mx == r ? std::fmod((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
    h *= 60.0;
    if (h < 0)
        h += 360.0;
    return h;
}

int degrees(double h)
{
    const int d = int(std::lround(h));
    return d >= 360 ? d - 360 : d;
}

int percent(double v)
{
    return int(std::lround(v * 100.0));
}

} // namespace

QPoint ColorPicker::filePixel(QPoint texel, QSize texture, QSize source, double vectorScale)
{
    const auto axis = [vectorScale](int t, int shown, int file) {
        const double scale = vectorScale > 0.0 ? vectorScale : shown > 0 && file > 0 ? double(file) / shown : 1.0;
        const int p = int((t + 0.5) * scale);
        return file > 0 ? std::clamp(p, 0, file - 1) : std::max(p, 0);
    };
    return QPoint(axis(texel.x(), texture.width(), source.width()), axis(texel.y(), texture.height(), source.height()));
}

bool ColorPicker::storesSrgb8(const Source &source)
{
    const color::Descriptor &cd = source.colour;
    const auto same = [](const double a[2], const double b[2]) {
        return std::abs(a[0] - b[0]) < 1e-4 && std::abs(a[1] - b[1]) < 1e-4;
    };
    const bool srgb = cd.source != color::Descriptor::Source::Icc && cd.transfer == color::Transfer::Srgb
                      && cd.fullRange && same(cd.primaries.r, color::kBt709.r) && same(cd.primaries.g, color::kBt709.g)
                      && same(cd.primaries.b, color::kBt709.b) && same(cd.primaries.w, color::kBt709.w);
    return srgb && !source.isFloat && source.bits <= 8 && !source.gray && !source.cmyk;
}

bool ColorPicker::showsFileValue(const Source &source)
{
    // Not for 8-bit sRGB (the RGB row is the file's value), nor an animation's later frame (only the
    // first is read again).
    return source.firstFrame && !storesSrgb8(source);
}

QList<ColorPicker::Row> ColorPicker::values(const Sample &sample, const Source &source, const QLocale &locale,
                                            const FileValue *file)
{
    QList<Row> rows;
    const double alpha = std::clamp(double(sample.alpha), 0.0, 1.0);
    const bool translucent = alpha < 1.0 - 1e-4;
    const QString a = css(alpha, 2);

    // sRGB, 8 bits a component, as CSS and design applications take colours: clipped to sRGB.
    int code[3];
    bool clipped = false;
    for (int c = 0; c < 3; ++c) {
        const double v = sample.rgb[c];
        clipped = clipped || v < -1e-3 || v > 1.0 + 1e-3;
        code[c] = int(std::lround(clamp01(color::linearToSrgb(float(clamp01(v)))) * 255.0));
    }
    const double r = code[0] / 255.0, g = code[1] / 255.0, b = code[2] / 255.0;
    QString hex = QLatin1Char('#') + hex2(code[0]) + hex2(code[1]) + hex2(code[2]);
    if (translucent)
        hex += hex2(int(std::lround(alpha * 255.0)));
    //: The color picker: the sRGB values of a color sRGB cannot hold (wide gamut, or brighter than SDR white).
    rows.append({QStringLiteral("HEX"),
                 clipped ? QCoreApplication::translate("ColorPicker", "%1 (clipped to sRGB)").arg(hex) : hex, hex});
    const QString rgb = translucent ? QStringLiteral("rgba(%1, %2, %3, %4)").arg(code[0]).arg(code[1]).arg(code[2]).arg(a)
                                    : QStringLiteral("rgb(%1, %2, %3)").arg(code[0]).arg(code[1]).arg(code[2]);
    rows.append({translucent ? QStringLiteral("RGBA") : QStringLiteral("RGB"), rgb, rgb});

    const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn, h = hue(r, g, b);
    const double l = (mx + mn) / 2.0;
    const double sl = d <= 0 ? 0.0 : d / (1.0 - std::abs(2.0 * l - 1.0));
    const QString hsl = translucent
                            ? QStringLiteral("hsla(%1, %2%, %3%, %4)").arg(degrees(h)).arg(percent(sl)).arg(percent(l)).arg(a)
                            : QStringLiteral("hsl(%1, %2%, %3%)").arg(degrees(h)).arg(percent(sl)).arg(percent(l));
    rows.append({QStringLiteral("HSL"), hsl, hsl});
    const double sv = mx <= 0 ? 0.0 : d / mx;
    const QString hsv = QStringLiteral("hsv(%1, %2%, %3%)").arg(degrees(h)).arg(percent(sv)).arg(percent(mx));
    rows.append({QStringLiteral("HSV"), hsv, hsv});
    // The textbook formula: CMYK without a press's profile is not the printed colour.
    const double k = 1.0 - mx;
    const auto ink = [k](double v) { return k >= 1.0 ? 0.0 : (1.0 - v - k) / (1.0 - k); };
    const QString cmyk =
        QStringLiteral("cmyk(%1%, %2%, %3%, %4%)").arg(percent(ink(r))).arg(percent(ink(g))).arg(percent(ink(b))).arg(percent(k));
    rows.append({QStringLiteral("CMYK"), cmyk, cmyk});

    // CIE Lab of the colour itself, unclipped: wide-gamut and HDR colours keep their values. One
    // decimal: the working buffer's half floats move the second; an 8-bit sRGB file's from its codes.
    std::array<double, 3> lab;
    if (storesSrgb8(source))
        lab = color::scRgbToLab(color::srgbToLinear(float(r)), color::srgbToLinear(float(g)), color::srgbToLinear(float(b)));
    else
        lab = color::scRgbToLab(sample.rgb[0], sample.rgb[1], sample.rgb[2]);
    const QString labText = QStringLiteral("lab(%1% %2 %3%4)")
                                .arg(css(lab[0], 1), css(lab[1], 1), css(lab[2], 1),
                                     translucent ? QStringLiteral(" / ") + a : QString());
    rows.append({QStringLiteral("Lab"), labText, labText});

    // The value the file stores, when it is not the sRGB above: read from the file's own samples.
    if (showsFileValue(source)) {
        Row row;
        //: The color picker: the value as the file stores it.
        row.label = QCoreApplication::translate("ColorPicker", "File");
        if (!file) {
            row.text = QStringLiteral("…"); // being read
        } else if (file->samples.isEmpty()) {
            row.text = QStringLiteral("—"); // the file could not be read again
        } else {
            QStringList parts;
            for (const double v : file->samples)
                parts << (file->isFloat ? css(v, 4) : QString::number(qlonglong(v)));
            row.copy = parts.join(QStringLiteral(", "));
            if (file->isFloat)
                //: The color picker: a file's value as floating-point numbers, e.g. "1.25, 0.5, 0 (float)".
                row.text = QCoreApplication::translate("ColorPicker", "%1 (float)").arg(row.copy);
            else
                //: The color picker: a file's value as integer codes of %2 bits, e.g. "1023, 512, 0 (10-bit)".
                row.text = QCoreApplication::translate("ColorPicker", "%1 (%2-bit)").arg(row.copy).arg(file->bits);
        }
        rows.append(row);
    }

    if (source.hdr) {
        const double nits = std::max(0.0, double(color::luminance(sample.rgb[0], sample.rgb[1], sample.rgb[2])))
                            * color::kSdrReferenceWhiteNits;
        const int decimals = nits < 10 ? 2 : nits < 100 ? 1 : 0;
        //: The color picker: the label of the luminance of an HDR colour.
        const QString label = QCoreApplication::translate("ColorPicker", "Luminance");
        //: The color picker: luminance in candelas per square metre (nits); keep "cd/m²".
        const QString text = QCoreApplication::translate("ColorPicker", "%1 cd/m²").arg(locale.toString(nits, 'f', decimals));
        rows.append({label, text, QString::number(nits, 'f', decimals)});
    }

    // The pixel's column and row in the locale's digits, without its group separators (the owner,
    // D-119 (6)): a comma for thousands made "12,345, 6,789" of the pixel 12345, 6789.
    QLocale coordinates = locale;
    coordinates.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    //: The color picker: the picked pixel's column and row in the image.
    rows.append({QCoreApplication::translate("ColorPicker", "Pixel"),
                 QStringLiteral("%1, %2").arg(coordinates.toString(sample.position.x()),
                                              coordinates.toString(sample.position.y())),
                 QStringLiteral("%1, %2").arg(sample.position.x()).arg(sample.position.y())});
    return rows;
}

bool ColorPicker::pick(const Sample &sample, const Source &source, const QLocale &locale)
{
    m_picked = sample;
    m_source = source;
    m_locale = locale;
    m_file.reset();
    m_rows = values(sample, source, locale);
    m_hover = -1;
    m_laidOut = false;
    m_dirty = true;
    return true;
}

bool ColorPicker::setFileValue(const FileValue &file)
{
    if (!m_picked)
        return false;
    m_file = file;
    m_rows = values(*m_picked, m_source, m_locale, &*m_file);
    m_laidOut = false;
    m_dirty = true;
    return true;
}

bool ColorPicker::clear()
{
    if (!m_picked)
        return false;
    m_picked.reset();
    m_file.reset();
    m_rows.clear();
    m_hover = -1;
    m_laidOut = false;
    m_dirty = true;
    return true;
}

bool ColorPicker::setLook(const Look &look)
{
    if (look == m_look)
        return false;
    m_look = look;
    m_laidOut = false;
    m_dirty = true;
    return true;
}

bool ColorPicker::setHover(int row)
{
    if (row == m_hover)
        return false;
    m_hover = row;
    m_dirty = true;
    return true;
}

double ColorPicker::rowHeight() const
{
    return std::ceil(QFontMetricsF(m_look.font).lineSpacing()) + 6.0;
}

void ColorPicker::layout()
{
    if (m_laidOut)
        return;
    m_laidOut = true;
    const QFontMetricsF metrics(m_look.font);
    m_labelWidth = 0;
    m_textWidth = 0;
    for (const Row &row : std::as_const(m_rows)) {
        m_labelWidth = std::max(m_labelWidth, std::ceil(metrics.horizontalAdvance(row.label)));
        m_textWidth = std::max(m_textWidth, std::ceil(metrics.horizontalAdvance(row.text)));
    }
    if (m_rows.isEmpty()) //: The color picker, before a color is picked.
        m_textWidth = std::ceil(metrics.horizontalAdvance(QCoreApplication::translate("ColorPicker", "Click the image to pick a color.")));
    const double values = m_rows.isEmpty() ? m_textWidth : m_labelWidth + 12.0 + m_textWidth;
    const double loupe = kCells * kCell;
    m_size = QSizeF(std::ceil(kPadding + loupe + 16.0 + values + 12.0 + kPadding),
                    std::ceil(2 * kPadding + std::max(loupe + kGap + kSwatch, m_rows.size() * rowHeight())));
}

QSizeF ColorPicker::size() const
{
    const_cast<ColorPicker *>(this)->layout();
    return m_size;
}

QRectF ColorPicker::loupeRect() const
{
    return QRectF(kPadding, kPadding, kCells * kCell, kCells * kCell);
}

QRectF ColorPicker::currentRect() const
{
    return QRectF(kPadding, kPadding + kCells * kCell + kGap, kSwatch, kSwatch);
}

QRectF ColorPicker::pickedRect() const
{
    return QRectF(kPadding + kCells * kCell - kSwatch, kPadding + kCells * kCell + kGap, kSwatch, kSwatch);
}

int ColorPicker::rowAt(const QPointF &position) const
{
    const QSizeF panel = size();
    const double left = kPadding + kCells * kCell + 16.0 - 6.0;
    if (position.x() < left || position.x() >= panel.width() - kPadding + 4.0 || position.y() < kPadding)
        return -1;
    const int row = int((position.y() - kPadding) / rowHeight());
    return row >= 0 && row < m_rows.size() ? row : -1;
}

QImage ColorPicker::texture()
{
    if (!m_dirty && !m_texture.isNull())
        return m_texture;
    m_dirty = false;
    layout();
    const qreal dpr = m_look.devicePixelRatio;
    m_texture = QImage((m_size * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    m_texture.setDevicePixelRatio(dpr);
    m_texture.fill(Qt::transparent);
    {
        QPainter painter(&m_texture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_look.panel);
        painter.drawRoundedRect(QRectF(QPointF(0, 0), m_size), m_look.radius, m_look.radius);

        // The holes the renderer fills from the image, each in a thin frame.
        QColor frame = m_look.label;
        frame.setAlphaF(0.5f * m_look.label.alphaF());
        for (const QRectF &hole : {loupeRect(), currentRect(), pickedRect()}) {
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.setBrush(Qt::black);
            painter.setPen(Qt::NoPen);
            painter.drawRect(hole);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(frame, 1.0));
            painter.drawRect(hole.adjusted(-0.5, -0.5, 0.5, 0.5));
        }
        // The pixel that a click picks: framed in black and white, seen on any colour.
        const QRectF centre(loupeRect().topLeft() + QPointF(kCells / 2 * kCell, kCells / 2 * kCell), QSizeF(kCell, kCell));
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawRect(centre.adjusted(-1.5, -1.5, 1.5, 1.5));
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawRect(centre.adjusted(-0.5, -0.5, 0.5, 0.5));

        painter.setFont(m_look.font);
        const double left = kPadding + kCells * kCell + 16.0;
        const double line = rowHeight();
        if (m_rows.isEmpty()) {
            painter.setPen(m_look.label);
            painter.drawText(QRectF(left, kPadding, m_textWidth + 1, line), Qt::AlignLeft | Qt::AlignVCenter,
                             QCoreApplication::translate("ColorPicker", "Click the image to pick a color."));
        }
        for (qsizetype i = 0; i < m_rows.size(); ++i) {
            const QRectF row(left - 6.0, kPadding + i * line, m_size.width() - kPadding - left + 10.0, line);
            if (i == m_hover) { // a click copies it: the row as the gallery's hovered tile
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_look.hover);
                painter.drawRoundedRect(row.adjusted(0, 1, 0, -1), m_look.radius / 2, m_look.radius / 2);
            }
            painter.setPen(m_look.label);
            painter.drawText(QRectF(left, row.top(), m_labelWidth + 1, line), Qt::AlignLeft | Qt::AlignVCenter,
                             m_rows.at(i).label);
            painter.setPen(m_look.ink);
            painter.drawText(QRectF(left + m_labelWidth + 12.0, row.top(), m_textWidth + 1, line),
                             Qt::AlignLeft | Qt::AlignVCenter, m_rows.at(i).text);
        }
    }
    return m_texture;
}
