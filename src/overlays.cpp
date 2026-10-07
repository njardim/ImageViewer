// ViewerWindow, part 3: what is drawn over the image (decision D-34). The information
// panel (I) with every detail, the compact overlay at the top (E14), the side-zone
// buttons (D-31) and notices. Each is a small premultiplied texture the renderer
// composites at SDR white, so none of them changes the viewport.
#include "viewer.h"

#include <QFileInfo>
#include <QDir>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr double kNavigationButtonSize = 56.0; // logical pixels
constexpr double kMargin = 12.0;               // between the window edge and a panel, logical pixels
constexpr double kTopMargin = 8.0;
constexpr int kNoticeMs = 3000;

// "100" only when the image really is shown 1:1 (nearest sampling); otherwise one decimal.
QString zoomNumber(double zoom, const QLocale &locale)
{
    return zoom == 1.0 ? locale.toString(100) : locale.toString(zoom * 100.0, 'f', 1);
}

// File sizes as the platform's file manager shows them: Explorer counts in 1024s, Finder and
// the Linux desktops in 1000s.
QString fileSizeText(qint64 bytes, const QLocale &locale)
{
#if defined(Q_OS_WIN)
    return locale.formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
#else
    return locale.formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
#endif
}

// The round previous/next button of a side zone, drawn at `pixels` (device) size.
QImage navigationButton(bool next, int pixels, bool pressed)
{
    QImage image(pixels, pixels, QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(pixels / kNavigationButtonSize);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, pressed ? 180 : 125));
    const double s = kNavigationButtonSize;
    painter.drawEllipse(QRectF(0.5, 0.5, s - 1.0, s - 1.0));
    painter.setPen(QPen(QColor(240, 240, 240), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double c = s / 2.0, half = s * 0.16, depth = s * 0.08;
    const double dir = next ? 1.0 : -1.0;
    QPainterPath chevron;
    chevron.moveTo(c - dir * depth, c - half);
    chevron.lineTo(c + dir * depth, c);
    chevron.lineTo(c - dir * depth, c + half);
    painter.drawPath(chevron);
    return image;
}

struct Row {
    QString label; // empty: the value spans both columns (messages)
    QString value;
    bool gapBefore = false; // first row of a section
};

} // namespace

void ViewerWindow::showNotice(const QString &text)
{
    m_notice = text;
    m_noticeTimer.start(kNoticeMs);
    updateOverlay();
}

void ViewerWindow::updateOverlay()
{
    if (!m_rendererReady)
        return;
    m_overlayOutput = m_renderer.output().description;
    const QLocale locale;
    const QString dot = QStringLiteral("  ·  ");
    QList<Row> rows;
    if (!m_notice.isEmpty())
        rows.append({QString(), m_notice});
    if (!m_message.isEmpty())
        rows.append({QString(), m_message});

    if (m_showInfo && !m_image.path.isEmpty()) {
        const QFileInfo file(m_image.path);
        rows.append({tr("File"), file.fileName(), !rows.isEmpty()});
        rows.append({tr("Folder"), QDir::toNativeSeparators(file.absolutePath())});
        if (m_image.fileSize >= 0)
            rows.append({tr("Size"), fileSizeText(m_image.fileSize, locale)});
        if (m_image.modified.isValid())
            rows.append({tr("Modified"), locale.toString(m_image.modified, QLocale::ShortFormat)});
        if (m_index >= 0 && m_files.value(m_index) == m_image.path)
            //: Position of the image in its folder, e.g. "3 of 120".
            rows.append({tr("Position"), tr("%1 of %2").arg(locale.toString(m_index + 1), locale.toString(m_files.size()))});
    }
    if (m_showInfo && m_image.width > 0) {
        //: Megapixels, e.g. "24.0 MP".
        QString dimensions = QStringLiteral("%1 × %2").arg(locale.toString(m_image.sourceWidth),
                                                          locale.toString(m_image.sourceHeight))
                             + dot
                             + tr("%1 MP").arg(locale.toString(double(m_image.sourceWidth) * m_image.sourceHeight / 1e6,
                                                               'f', 1));
        if (m_image.width != m_image.sourceWidth)
            dimensions += dot + tr("reduced to %1 × %2 for the GPU")
                                    .arg(locale.toString(m_image.width), locale.toString(m_image.height));
        rows.append({tr("Dimensions"), dimensions, true});
        //: Bits per channel of the image file, e.g. "16-bit".
        QStringList format = {m_image.codec, tr("%1-bit").arg(m_image.sourceBits)};
        if (m_image.sourceFloat)
            format << tr("floating point");
        if (m_image.hasAlpha)
            format << tr("alpha");
        rows.append({tr("Format"), format.join(dot)});
        if (m_image.orientation > 1)
            //: The EXIF orientation tag (2 to 8) of the file, already applied to the image.
            rows.append({tr("Orientation"), tr("EXIF %1, applied").arg(m_image.orientation)});
        rows.append({tr("Color"), m_image.colour.description});
        rows.append({tr("Peak"), tr("%1× SDR white (≈%2 nits)")
                                     .arg(locale.toString(double(m_image.maxComponent), 'f', 2),
                                          locale.toString(double(m_image.maxComponent * color::kSdrReferenceWhiteNits),
                                                          'f', 0))});
        //: Unit after a duration in milliseconds.
        rows.append({tr("Decoded in"), tr("%1 ms").arg(locale.toString(m_image.decodeMs, 'f', 0))});

        if (const CameraInfo &camera = m_image.camera; !camera.isEmpty()) {
            // "Canon" and "Canon EOS R5": the model often repeats the make.
            QString body = camera.model;
            if (!camera.make.isEmpty() && !camera.model.startsWith(camera.make, Qt::CaseInsensitive))
                body = camera.model.isEmpty() ? camera.make : camera.make + QLatin1Char(' ') + camera.model;
            bool first = true;
            const auto add = [&](const QString &label, const QString &value) {
                if (value.isEmpty())
                    return;
                rows.append({label, value, std::exchange(first, false)});
            };
            add(tr("Camera"), body);
            add(tr("Lens"), camera.lens);
            QStringList shot;
            if (camera.exposureTime > 0)
                //: Exposure time of a photograph, e.g. "1/250 s".
                shot << tr("%1 s").arg(exposureTimeText(camera.exposureTime, locale));
            if (camera.fNumber > 0)
                shot << QStringLiteral("f/") + locale.toString(double(camera.fNumber), 'f', 1);
            if (camera.iso > 0)
                shot << QStringLiteral("ISO ") + locale.toString(camera.iso);
            if (camera.focalLength > 0)
                //: Focal length of the lens, e.g. "50 mm".
                shot << tr("%1 mm").arg(locale.toString(double(camera.focalLength), 'f', 0));
            //: Label of the photograph's shooting settings: exposure time, aperture, ISO, focal length.
            add(tr("Exposure"), shot.join(dot));
            if (camera.taken.isValid())
                //: Label of the date the photograph was taken.
                add(tr("Taken"), locale.toString(camera.taken, QLocale::ShortFormat));
        }
    }
    if (m_showInfo) {
        QStringList view;
        if (m_image.width > 0) {
            //: A zoom percentage, e.g. "100 %"; write the percent sign as your language does.
            view << tr("%1 %").arg(zoomNumber(currentZoom(), locale));
            if (m_quarterTurns != 0)
                //: The view is rotated clockwise by this many degrees.
                view << tr("rotated %1°").arg(locale.toString(m_quarterTurns * 90));
            if (m_mirrored)
                view << tr("mirrored");
        }
        if (m_exposureEv != 0.0f)
            view << tr("exposure %1 EV")
                        .arg((m_exposureEv > 0 ? QStringLiteral("+") : QString())
                             + locale.toString(double(m_exposureEv), 'f', 1));
        if (m_clipWarning)
            view << tr("altered pixels highlighted");
        if (!view.isEmpty())
            rows.append({tr("View"), view.join(dot), true});
        rows.append({tr("Output"), m_renderer.output().description + dot + m_renderer.backendName(), view.isEmpty()});
        if (m_image.width > 0) {
            // Whether the image is shown as is, tone mapped or clipped (criteria H4, H6).
            const Renderer::Output &out = m_renderer.output();
            const color::OutputStage stage = Renderer::stageFor(out, imageFrame());
            const double toNits = out.nitsPerUnit;
            const double peakNits = stage.peak * toNits;
            if (stage.sourcePeak > stage.peak) {
                QString line = tr("BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits")
                                   .arg(locale.toString(stage.sourcePeak * toNits, 'f', 0),
                                        locale.toString(peakNits, 'f', 0),
                                        locale.toString(color::eetfKneeNits(float(stage.sourcePeak * toNits),
                                                                            float(peakNits)),
                                                        'f', 0));
                if (m_image.maxComponent * stage.exposure * stage.scale * toNits > color::kPqPeakNits)
                    line += dot + tr("clipped above %1 nits").arg(locale.toString(double(color::kPqPeakNits), 'f', 0));
                rows.append({tr("Highlights"), line});
            } else if (m_image.maxComponent * stage.exposure * stage.scale > stage.peak) {
                rows.append({tr("Highlights"),
                             (m_toneMap ? tr("clipped above %1 nits (colors outside the output gamut)")
                                        : tr("clipped above %1 nits (tone mapping off)"))
                                 .arg(locale.toString(peakNits, 'f', 0))});
            }
        }
    }

    if (rows.isEmpty()) {
        m_overlaySize = {};
        m_renderer.setOverlay(Renderer::InfoLayer, QImage());
        updateTopOverlay();
        requestUpdate();
        return;
    }

    const qreal dpr = devicePixelRatio();
    const QFont font = QGuiApplication::font();
    const QFontMetricsF metrics(font);
    const qreal padding = 10.0, columnGap = 14.0, sectionGap = 6.0, lineHeight = metrics.height() + 2.0;
    qreal labelWidth = 0, valueWidth = 0, spanWidth = 0, height = 2 * padding - 2.0;
    for (const Row &row : std::as_const(rows)) {
        if (row.label.isEmpty()) {
            spanWidth = std::max(spanWidth, metrics.horizontalAdvance(row.value));
        } else {
            labelWidth = std::max(labelWidth, metrics.horizontalAdvance(row.label));
            valueWidth = std::max(valueWidth, metrics.horizontalAdvance(row.value));
        }
        height += lineHeight + (row.gapBefore ? sectionGap : 0.0);
    }
    // At most half a large window (a long folder path must not cover the image), never larger
    // than a small one: long values are elided, rows that do not fit are left out.
    const qreal maxWidth = std::max(120.0, std::min(this->width() - 2 * kMargin, std::max(480.0, this->width() * 0.5)));
    const qreal maxHeight = std::max(lineHeight + 2 * padding, this->height() - 2 * kMargin);
    const qreal contentWidth = std::max(spanWidth, labelWidth > 0 ? labelWidth + columnGap + valueWidth : 0.0);
    const QSizeF logical(std::ceil(std::min(contentWidth + 2 * padding, maxWidth)), std::ceil(std::min(height, maxHeight)));

    QImage overlay((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setLayoutDirection(QGuiApplication::layoutDirection());
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), 8, 8);
    painter.setFont(font);
    // Columns are laid out left to right and mirrored for right-to-left languages; within a
    // cell, AlignLeft is the start of the line (Qt mirrors it in right-to-left layouts).
    const bool rtl = QGuiApplication::layoutDirection() == Qt::RightToLeft;
    const qreal inner = logical.width() - 2 * padding;
    const qreal labelColumn = std::min(labelWidth, inner / 2.0);
    const auto cell = [&](qreal x, qreal y, qreal w) {
        return QRectF(rtl ? logical.width() - padding - x - w : padding + x, y, w, lineHeight);
    };
    qreal y = padding;
    for (const Row &row : std::as_const(rows)) {
        y += row.gapBefore ? sectionGap : 0.0;
        if (y + lineHeight > logical.height() - padding + 2.0)
            break;
        if (row.label.isEmpty()) {
            painter.setPen(QColor(235, 235, 235));
            painter.drawText(cell(0, y, inner), Qt::AlignLeft | Qt::AlignTop,
                             metrics.elidedText(row.value, Qt::ElideRight, inner));
        } else {
            const qreal valueX = labelColumn + columnGap;
            painter.setPen(QColor(165, 165, 165));
            painter.drawText(cell(0, y, labelColumn), Qt::AlignLeft | Qt::AlignTop,
                             metrics.elidedText(row.label, Qt::ElideRight, labelColumn));
            painter.setPen(QColor(235, 235, 235));
            const qreal w = std::max<qreal>(0.0, inner - valueX);
            painter.drawText(cell(valueX, y, w), Qt::AlignLeft | Qt::AlignTop,
                             metrics.elidedText(row.value, Qt::ElideMiddle, w));
        }
        y += lineHeight;
    }
    painter.end();

    m_overlaySize = overlay.size();
    m_renderer.setOverlay(Renderer::InfoLayer, overlay);
    updateTopOverlay();
    requestUpdate();
}

OverlayVisibility ViewerWindow::topOverlayMode() const
{
    return visibility() == QWindow::FullScreen ? m_settings.overlayFullScreen : m_settings.overlayWindow;
}

bool ViewerWindow::topOverlayVisible() const
{
    if (m_topOverlaySize.isEmpty())
        return false;
    switch (topOverlayMode()) {
    case OverlayVisibility::Always: return true;
    case OverlayVisibility::Hover: return m_pointerAtTop;
    case OverlayVisibility::Hidden: break;
    }
    return false;
}

double ViewerWindow::topActivationHeight() const
{
    // The top band, and the overlay itself once it is shown.
    return std::max(48.0, m_topOverlaySize.height() / devicePixelRatio() + 2 * kTopMargin);
}

void ViewerWindow::setPointerAtTop(bool atTop)
{
    if (atTop) {
        m_topOverlayTimer.stop();
        if (!m_pointerAtTop) {
            m_pointerAtTop = true;
            requestUpdate();
        }
    } else if (m_pointerAtTop && !m_topOverlayTimer.isActive()) {
        m_topOverlayTimer.start(m_settings.overlayHideDelayMs);
    }
}

void ViewerWindow::updateTopOverlay()
{
    if (!m_rendererReady)
        return;
    const QLocale locale;
    QStringList parts;
    if (!m_image.path.isEmpty()) {
        const bool hasImage = m_image.width > 0;
        for (OverlayField field : std::as_const(m_settings.overlayFields)) {
            switch (field) {
            case OverlayField::Name: parts << QFileInfo(m_image.path).fileName(); break;
            case OverlayField::Dimensions:
                if (hasImage)
                    parts << QStringLiteral("%1 × %2").arg(locale.toString(m_image.sourceWidth),
                                                          locale.toString(m_image.sourceHeight));
                break;
            case OverlayField::FileSize:
                if (m_image.fileSize >= 0)
                    parts << fileSizeText(m_image.fileSize, locale);
                break;
            case OverlayField::Zoom:
                if (hasImage)
                    parts << tr("%1 %").arg(zoomNumber(currentZoom(), locale));
                break;
            case OverlayField::ColorSpace:
                if (hasImage)
                    parts << m_image.colour.description;
                break;
            case OverlayField::Modified:
                if (m_image.modified.isValid())
                    parts << locale.toString(m_image.modified, QLocale::ShortFormat);
                break;
            case OverlayField::Position:
                if (m_index >= 0 && m_files.value(m_index) == m_image.path)
                    parts << QStringLiteral("%1 / %2").arg(locale.toString(m_index + 1), locale.toString(m_files.size()));
                break;
            case OverlayField::Output: parts << m_renderer.output().description; break;
            }
        }
    }
    const qreal dpr = devicePixelRatio();
    const QString key = parts.join(QChar(0x1f)) + QStringLiteral("|%1|%2|%3|%4|%5|%6")
                                                      .arg(m_settings.overlayBackgroundOpacity)
                                                      .arg(m_settings.overlayTextOpacity)
                                                      .arg(m_settings.overlayOutline)
                                                      .arg(width())
                                                      .arg(dpr)
                                                      .arg(int(QGuiApplication::layoutDirection()));
    if (key == m_topOverlayKey)
        return;
    m_topOverlayKey = key;
    if (parts.isEmpty()) {
        m_topOverlaySize = {};
        m_renderer.setOverlay(Renderer::TopLayer, QImage());
        requestUpdate();
        return;
    }

    const QFont font = QGuiApplication::font();
    const QFontMetricsF metrics(font);
    const QString separator = QStringLiteral("   ·   ");
    const qreal paddingX = 14.0, paddingY = 6.0;
    const qreal maxText = std::max(60.0, width() - 2 * kMargin - 2 * paddingX);
    QString text = parts.join(separator);
    // Too wide: shorten the file name first (it is usually the longest part), then the line.
    if (metrics.horizontalAdvance(text) > maxText && m_settings.overlayFields.contains(OverlayField::Name)) {
        const int nameIndex = int(m_settings.overlayFields.indexOf(OverlayField::Name));
        if (nameIndex < parts.size()) {
            QStringList others = parts;
            others.removeAt(nameIndex);
            const qreal rest = metrics.horizontalAdvance(others.join(separator) + separator);
            parts[nameIndex] = metrics.elidedText(parts.at(nameIndex), Qt::ElideMiddle, std::max(60.0, maxText - rest));
            text = parts.join(separator);
        }
    }
    text = metrics.elidedText(text, Qt::ElideRight, maxText);
    const QSizeF logical(std::ceil(metrics.horizontalAdvance(text) + 2 * paddingX),
                         std::ceil(metrics.height() + 2 * paddingY));

    QImage overlay((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setLayoutDirection(QGuiApplication::layoutDirection());
    if (m_settings.overlayBackgroundOpacity > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, std::lround(m_settings.overlayBackgroundOpacity * 2.55)));
        painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), logical.height() / 2.0, logical.height() / 2.0);
    }
    painter.setFont(font);
    const int textAlpha = int(std::lround(m_settings.overlayTextOpacity * 2.55));
    const QRectF textRect(paddingX, paddingY, logical.width() - 2 * paddingX, metrics.height());
    if (m_settings.overlayOutline) {
        // A dark ring around the glyphs keeps the text readable over any image.
        painter.setPen(QColor(0, 0, 0, textAlpha));
        for (const QPointF offset : {QPointF(-1, -1), QPointF(0, -1), QPointF(1, -1), QPointF(-1, 0), QPointF(1, 0),
                                     QPointF(-1, 1), QPointF(0, 1), QPointF(1, 1)})
            painter.drawText(textRect.translated(offset), Qt::AlignCenter, text);
    }
    painter.setPen(QColor(240, 240, 240, textAlpha));
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.end();

    m_topOverlaySize = overlay.size();
    m_renderer.setOverlay(Renderer::TopLayer, overlay);
    requestUpdate();
}

void ViewerWindow::placeOverlays(Renderer::Frame *frame) const
{
    const double dpr = devicePixelRatio();
    const double margin = kMargin * dpr;
    if (!m_overlaySize.isEmpty()) {
        const double x = QGuiApplication::layoutDirection() == Qt::RightToLeft
                             ? deviceSize().width() - margin - m_overlaySize.width()
                             : margin;
        frame->overlayRects[Renderer::InfoLayer] =
            QRectF(QPointF(std::round(x), std::round(deviceSize().height() - margin - m_overlaySize.height())),
                   QSizeF(m_overlaySize));
    }
    if (topOverlayVisible())
        frame->overlayRects[Renderer::TopLayer] =
            QRectF(QPointF(std::round((deviceSize().width() - m_topOverlaySize.width()) / 2.0), std::round(kTopMargin * dpr)),
                   QSizeF(m_topOverlaySize));
    if (m_hoverZone == Zone::Previous)
        frame->overlayRects[Renderer::PreviousButtonLayer] = zoneButtonRect(Zone::Previous);
    else if (m_hoverZone == Zone::Next)
        frame->overlayRects[Renderer::NextButtonLayer] = zoneButtonRect(Zone::Next);
}

void ViewerWindow::updateNavigationButtons()
{
    if (!m_rendererReady)
        return;
    const int pixels = int(std::lround(kNavigationButtonSize * devicePixelRatio()));
    const bool pressed = m_pressZone != Zone::None && m_pressZone == m_hoverZone && !m_pressMoved;
    m_renderer.setOverlay(Renderer::PreviousButtonLayer, m_hoverZone == Zone::Previous
                                                             ? navigationButton(false, pixels, pressed)
                                                             : QImage());
    m_renderer.setOverlay(Renderer::NextButtonLayer,
                          m_hoverZone == Zone::Next ? navigationButton(true, pixels, pressed) : QImage());
    requestUpdate();
}

QRectF ViewerWindow::zoneButtonRect(Zone zone) const
{
    const double dpr = devicePixelRatio();
    const double pixels = std::round(kNavigationButtonSize * dpr);
    const double zoneWidth = std::min<double>(m_settings.sideZoneWidth, width() / 4.0) * dpr;
    const double centreX = zone == Zone::Previous ? zoneWidth / 2.0 : deviceSize().width() - zoneWidth / 2.0;
    // Whole device pixels: the button texture is sampled 1:1.
    return QRectF(QPointF(std::round(centreX - pixels / 2.0), std::round((deviceSize().height() - pixels) / 2.0)),
                  QSizeF(pixels, pixels));
}
