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
constexpr double kWarningSign = 128.0;          // logical pixels, square (D-63)
constexpr double kWarningGap = 12.0;            // between the sign and the message
constexpr int kNoticeMs = 3000;
const QString kFieldSeparator = QStringLiteral("   ·   "); // between the top overlay's facts, and the title's

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

constexpr double kToolbarButton = 44.0;  // the slideshow toolbar's round buttons, logical pixels (D-65)
constexpr double kToolbarPadding = 8.0;  // around and between them

// The slideshow's toolbar: stop, then pause (or play, when paused), round buttons in the side
// buttons' look on a panel in the information panels' look.
QImage slideshowToolbar(bool paused, int hover, int press, double dpr, const QColor &background)
{
    const QSizeF logical(2 * kToolbarButton + 3 * kToolbarPadding, kToolbarButton + 2 * kToolbarPadding);
    QImage image((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), kPanelRadius, kPanelRadius);
    const QColor glyph(240, 240, 240);
    const double s = kToolbarButton;
    for (int i = 0; i < 2; ++i) {
        const QRectF circle(kToolbarPadding + i * (s + kToolbarPadding), kToolbarPadding, s, s);
        painter.setBrush(i == press ? QColor(255, 255, 255, 80) : i == hover ? QColor(255, 255, 255, 48)
                                                                              : QColor(0, 0, 0, 125));
        painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
        painter.setBrush(glyph);
        const QPointF c = circle.center();
        if (i == 0) { // stop: a square
            const double half = s * 0.17;
            painter.drawRoundedRect(QRectF(c.x() - half, c.y() - half, 2 * half, 2 * half), 1.5, 1.5);
        } else if (paused) { // play: a triangle, its centre of mass on the circle's
            const double h = s * 0.42, w = h * 0.87;
            QPainterPath triangle;
            triangle.moveTo(c.x() - w / 3.0, c.y() - h / 2.0);
            triangle.lineTo(c.x() + 2.0 * w / 3.0, c.y());
            triangle.lineTo(c.x() - w / 3.0, c.y() + h / 2.0);
            triangle.closeSubpath();
            painter.drawPath(triangle);
        } else { // pause: two bars
            const double h = s * 0.38, w = s * 0.12, gap = s * 0.11;
            painter.drawRoundedRect(QRectF(c.x() - gap / 2.0 - w, c.y() - h / 2.0, w, h), 1.5, 1.5);
            painter.drawRoundedRect(QRectF(c.x() + gap / 2.0, c.y() - h / 2.0, w, h), 1.5, 1.5);
        }
    }
    return image;
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
    painter.setBrush(Qt::NoBrush); // the circle's brush filled the open chevron as a dark triangle
    const double c = s / 2.0, half = s * 0.16, depth = s * 0.08;
    const double dir = next ? 1.0 : -1.0;
    QPainterPath chevron;
    chevron.moveTo(c - dir * depth, c - half);
    chevron.lineTo(c + dir * depth, c);
    chevron.lineTo(c - dir * depth, c + half);
    painter.drawPath(chevron);
    return image;
}

// In right-to-left interfaces a value (a path, a file name) is laid out in its own direction,
// as a unit: without the isolate, "/tmp/a" showed as "tmp/a/" and "(1) photo" as "photo (1)".
QString isolated(const QString &text)
{
    return QGuiApplication::layoutDirection() == Qt::RightToLeft ? QChar(0x2068) + text + QChar(0x2069) : text;
}

// Both information panels share one look (D-49, Settings::panelBackground()): the optional dark
// ring around the glyphs keeps the text readable over anything, HDR highlights included.
void drawPanelText(QPainter &painter, const QRectF &rect, int flags, const QString &text, const QColor &colour,
                   bool outline)
{
    if (outline) {
        painter.setPen(QColor(0, 0, 0, colour.alpha()));
        for (const QPointF offset : {QPointF(-1, -1), QPointF(0, -1), QPointF(1, -1), QPointF(-1, 0), QPointF(1, 0),
                                     QPointF(-1, 1), QPointF(0, 1), QPointF(1, 1)})
            painter.drawText(rect.translated(offset), flags, text);
    }
    painter.setPen(colour);
    painter.drawText(rect, flags, text);
}

// The warning sign over an error (D-63), the shape of the ⚠️ emoji: drawn rather than taken from
// an emoji font, which not every system has (a missing glyph would show an empty box).
void drawWarningSign(QPainter &painter, const QRectF &box)
{
    const double w = box.width();
    const QColor amber(255, 196, 0);
    QPainterPath triangle;
    triangle.moveTo(box.center().x(), box.top() + 0.1 * w);
    triangle.lineTo(box.right() - 0.06 * w, box.top() + 0.88 * w);
    triangle.lineTo(box.left() + 0.06 * w, box.top() + 0.88 * w);
    triangle.closeSubpath();
    painter.setPen(QPen(amber, 0.1 * w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); // rounds the corners
    painter.setBrush(amber);
    painter.drawPath(triangle);
    const double bar = 0.09 * w;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(32, 32, 32));
    painter.drawRoundedRect(QRectF(box.center().x() - bar / 2, box.top() + 0.36 * w, bar, 0.3 * w), bar / 2, bar / 2);
    painter.drawEllipse(QPointF(box.center().x(), box.top() + 0.76 * w), 0.6 * bar, 0.6 * bar);
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

void ViewerWindow::updateTitle()
{
    // The overlay's own fields, or a fixed set, always in its wording and style (D-58).
    using F = OverlayField;
    static const QList<OverlayField> name = {F::Name}, details = {F::Name, F::Position, F::Dimensions},
                                     everything = {F::Name, F::Position, F::Dimensions, F::FileSize, F::Zoom};
    const TitleMode mode = m_settings.titleMode;
    const QList<OverlayField> &fields = mode == TitleMode::Overlay   ? m_settings.overlayFields
                                        : mode == TitleMode::Name    ? name
                                        : mode == TitleMode::Details ? details
                                                                     : everything;
    QString text = m_image.path.isEmpty() ? QString() : overlayParts(fields).join(kFieldSeparator);
    if (!text.isEmpty())
        text += QStringLiteral(" — ");
    text += QStringLiteral("ImageViewer");
    if (text != title())
        setTitle(text);
}

void ViewerWindow::updateOverlay()
{
    updateTitle();
    if (!m_rendererReady)
        return;
    updateGallery(); // the current image, the folder or the window's width may have changed
    updateMessage();
    updateSlideshowToolbar(); // the device pixel ratio may have changed
    m_overlayOutput = m_renderer.output().description;
    const QLocale locale;
    const QString dot = QStringLiteral("  ·  ");
    QList<Row> rows;
    if (!m_notice.isEmpty())
        rows.append({QString(), m_notice});
    if (!m_message.isEmpty())
        rows.append({QString(), m_message});

    if (m_settings.showInfo && !m_image.path.isEmpty()) {
        const QFileInfo file(m_image.path);
        // Names are shown without bidi and other format characters, as in the title: a file
        // named "photo\u202Egpj.exe" must not read "photoexe.jpg".
        rows.append({tr("File"), displayFileName(file.fileName()), !rows.isEmpty()});
        rows.append({tr("Folder"), displayFileName(QDir::toNativeSeparators(file.absolutePath()))});
        if (const QStringList position = overlayParts({OverlayField::Position}); !position.isEmpty())
            rows.append({tr("Position"), position.constFirst()}); // as the overlay writes it (D-58)
        if (m_image.fileSize >= 0)
            rows.append({tr("Size"), fileSizeText(m_image.fileSize, locale)});
        if (m_image.modified.isValid())
            rows.append({tr("Modified"), locale.toString(m_image.modified, QLocale::ShortFormat)});
    }
    if (m_settings.showInfo && m_image.width > 0) {
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
        QStringList format = {m_image.codec, tr("%1-bit").arg(locale.toString(m_image.sourceBits))};
        if (m_image.sourceFloat)
            format << tr("floating point");
        if (m_image.hasAlpha)
            format << tr("alpha");
        rows.append({tr("Format"), format.join(dot)});
        if (m_animation) {
            const int count = m_animation->frameCount();
            //: The frame of an animation on screen, e.g. "frame 3 of 24".
            QString frames = count > 0 ? tr("frame %1 of %2").arg(locale.toString(m_frameIndex + 1), locale.toString(count))
                                       : tr("frame %1").arg(locale.toString(m_frameIndex + 1));
            if (m_animationPaused)
                frames += dot + tr("paused");
            rows.append({tr("Animation"), frames});
        }
        if (m_image.orientation > 1) {
            //: The EXIF orientation tag (2 to 8) of the file, already applied to the image.
            const QString orientation = tr("EXIF %1, applied").arg(locale.toString(m_image.orientation));
            rows.append({tr("Orientation"), orientation});
        }
        rows.append({tr("Color"), m_image.colour.description});
        //: nits: candela per square metre, the unit of luminance.
        const QString peak = tr("%1× SDR white (≈%2 nits)")
                                 .arg(locale.toString(double(m_image.maxComponent), 'f', 2),
                                      locale.toString(double(m_image.maxComponent * color::kSdrReferenceWhiteNits), 'f', 0));
        rows.append({tr("Peak"), peak});
        //: Unit after a duration in milliseconds.
        const QString decoded = tr("%1 ms").arg(locale.toString(m_image.decodeMs, 'f', 0));
        rows.append({tr("Decoded in"), decoded});

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
    if (m_settings.showInfo) {
        QStringList view;
        if (m_image.width > 0) {
            //: A zoom percentage, e.g. "100 %"; write the percent sign as your language does.
            view << tr("%1 %").arg(zoomNumber(shownZoom(), locale));
            if (m_quarterTurns != 0)
                //: The view is rotated clockwise by this many degrees.
                view << tr("rotated %1°").arg(locale.toString(m_quarterTurns * 90));
            if (m_mirrored)
                view << tr("mirrored");
        }
        if (m_exposureEv != 0.0f)
            //: The viewer's exposure adjustment in EV (photographic stops), e.g. "exposure +1.5 EV".
            view << tr("exposure %1 EV")
                        .arg((m_exposureEv > 0 ? locale.positiveSign() : QString())
                             + locale.toString(double(m_exposureEv), 'f', 1));
        if (m_settings.clipWarning)
            view << tr("altered pixels highlighted");
        if (!view.isEmpty())
            rows.append({overlayFieldName(OverlayField::Zoom), view.join(dot), true});
        rows.append({tr("Output"), m_renderer.output().description + dot + m_renderer.backendName(), view.isEmpty()});
        if (m_image.width > 0) {
            // Whether the image is shown as is, tone mapped or clipped (criteria H4, H6).
            const Renderer::Output &out = m_renderer.output();
            const color::OutputStage stage = Renderer::stageFor(out, imageFrame());
            const double toNits = out.nitsPerUnit;
            const double peakNits = stage.peak * toNits;
            if (stage.sourcePeak > stage.peak) {
                //: nits: candela per square metre, the unit of luminance.
                QString line = tr("BT.2390 tone mapping from %1 to %2 nits, unchanged up to %3 nits")
                                   .arg(locale.toString(stage.sourcePeak * toNits, 'f', 0),
                                        locale.toString(peakNits, 'f', 0),
                                        locale.toString(color::eetfKneeNits(float(stage.sourcePeak * toNits),
                                                                            float(peakNits)),
                                                        'f', 0));
                if (m_image.maxComponent * stage.exposure * stage.scale * toNits > color::kPqPeakNits)
                    //: nits: candela per square metre, the unit of luminance.
                    line += dot + tr("clipped above %1 nits").arg(locale.toString(double(color::kPqPeakNits), 'f', 0));
                rows.append({tr("Highlights"), line});
            } else if (m_image.maxComponent * stage.exposure * stage.scale > stage.peak) {
                rows.append({tr("Highlights"),
                             //: nits: candela per square metre, the unit of luminance.
                             (m_settings.toneMap ? tr("clipped above %1 nits (colors outside the output gamut)")
                                        //: nits: candela per square metre, the unit of luminance.
                                        : tr("clipped above %1 nits (tone mapping off)"))
                                 .arg(locale.toString(peakNits, 'f', 0))});
            }
        }
    }

    const qreal dpr = devicePixelRatio();
    const QFont font = QGuiApplication::font();
    // The panel is drawn again only when what it shows changes, not on every animation frame,
    // wheel step or resize event (each text is drawn nine times with the outline).
    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                      .arg(m_settings.overlayBackgroundOpacity)
                      .arg(m_settings.overlayTextOpacity)
                      .arg(m_settings.overlayOutline)
                      .arg(width())
                      .arg(height())
                      .arg(dpr)
                      .arg(int(QGuiApplication::layoutDirection()))
                      .arg(font.key());
    for (const Row &row : std::as_const(rows))
        key += QChar(0x1e) + row.label + QChar(0x1f) + row.value + (row.gapBefore ? u'+' : u'-');
    if (key == m_panelKey) {
        updateTopOverlay();
        requestUpdate();
        return;
    }
    m_panelKey = key;

    if (rows.isEmpty()) {
        m_overlaySize = {};
        m_renderer.setOverlay(Renderer::InfoLayer, QImage());
        updateTopOverlay();
        requestUpdate();
        return;
    }

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
    const qreal maxWidth = std::max(120.0, std::min(this->width() - 2 * kEdgeMargin, std::max(480.0, this->width() * 0.5)));
    const qreal maxHeight = std::max(lineHeight + 2 * padding, this->height() - 2 * kEdgeMargin - galleryReserve());
    const qreal contentWidth = std::max(spanWidth, labelWidth > 0 ? labelWidth + columnGap + valueWidth : 0.0);
    const QSizeF logical(std::ceil(std::min(contentWidth + 2 * padding, maxWidth)), std::ceil(std::min(height, maxHeight)));

    QImage overlay((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setLayoutDirection(QGuiApplication::layoutDirection());
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_settings.panelBackground());
    painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), kPanelRadius, kPanelRadius);
    painter.setFont(font);
    const QColor valueColour = m_settings.panelText(Settings::kPanelValueGrey);
    const QColor labelColour = m_settings.panelText(Settings::kPanelLabelGrey);
    const bool outline = m_settings.overlayOutline;
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
            drawPanelText(painter, cell(0, y, inner), Qt::AlignLeft | Qt::AlignTop,
                          isolated(metrics.elidedText(row.value, Qt::ElideRight, inner)), valueColour, outline);
        } else {
            const qreal valueX = labelColumn + columnGap;
            drawPanelText(painter, cell(0, y, labelColumn), Qt::AlignLeft | Qt::AlignTop,
                          metrics.elidedText(row.label, Qt::ElideRight, labelColumn), labelColour, outline);
            const qreal w = std::max<qreal>(0.0, inner - valueX);
            drawPanelText(painter, cell(valueX, y, w), Qt::AlignLeft | Qt::AlignTop,
                          isolated(metrics.elidedText(row.value, Qt::ElideMiddle, w)), valueColour, outline);
        }
        y += lineHeight;
    }
    painter.end();

    m_overlaySize = overlay.size();
    m_renderer.setOverlay(Renderer::InfoLayer, overlay);
    updateTopOverlay();
    requestUpdate();
}

void ViewerWindow::updateMessage()
{
    // An error, in the middle of the window (D-63): the warning sign and, below it, the message
    // in the panels' look (D-49), its lines wrapped and centred.
    const qreal dpr = devicePixelRatio();
    const QFont font = QGuiApplication::font();
    const QString key = m_error.isEmpty() ? QString()
                                          : QStringLiteral("%1|%2|%3|%4|%5|%6|%7|")
                                                    .arg(width())
                                                    .arg(dpr)
                                                    .arg(m_settings.overlayBackgroundOpacity)
                                                    .arg(m_settings.overlayTextOpacity)
                                                    .arg(m_settings.overlayOutline)
                                                    .arg(int(QGuiApplication::layoutDirection()))
                                                    .arg(font.key())
                                                + m_error;
    if (key == m_messageKey)
        return;
    m_messageKey = key;
    if (m_error.isEmpty()) {
        m_messageSize = {};
        m_renderer.setOverlay(Renderer::MessageLayer, QImage());
        return;
    }
    const QFontMetricsF metrics(font);
    const qreal padding = 10.0;
    const int flags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
    const QString text = isolated(m_error);
    const qreal maxText = std::max(120.0, std::min(560.0, width() - 2 * kEdgeMargin - 2 * padding));
    const QSizeF textSize = metrics.boundingRect(QRectF(0, 0, maxText, 1e6), flags, text).size();
    const QSizeF panel(std::ceil(textSize.width() + 2 * padding), std::ceil(textSize.height() + 2 * padding));
    const QSizeF logical(std::max(panel.width(), kWarningSign), kWarningSign + kWarningGap + panel.height());

    QImage image((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setLayoutDirection(QGuiApplication::layoutDirection());
    drawWarningSign(painter, QRectF((logical.width() - kWarningSign) / 2.0, 0, kWarningSign, kWarningSign));
    const QRectF panelRect((logical.width() - panel.width()) / 2.0, kWarningSign + kWarningGap, panel.width(), panel.height());
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_settings.panelBackground());
    painter.drawRoundedRect(panelRect, kPanelRadius, kPanelRadius);
    painter.setFont(font);
    drawPanelText(painter, panelRect.adjusted(padding, padding, -padding, -padding), flags, text,
                  m_settings.panelText(Settings::kPanelValueGrey), m_settings.overlayOutline);
    painter.end();
    m_messageSize = image.size();
    m_renderer.setOverlay(Renderer::MessageLayer, image);
}

OverlayVisibility ViewerWindow::topOverlayMode() const
{
    if (!m_settings.showOverlay)
        return OverlayVisibility::Hidden;
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
    // The top band, and the overlay itself once it is shown; with the slideshow's toolbar below
    // the overlay, down to the toolbar too (reaching for it must not hide the overlay, which
    // would move the toolbar up from under the pointer).
    const double dpr = devicePixelRatio();
    double band = std::max(48.0, m_topOverlaySize.height() / dpr + 2 * kEdgeMargin);
    if (!m_toolbarSize.isEmpty())
        band = std::max(band, m_topOverlaySize.height() / dpr + kEdgeMargin + m_toolbarSize.height() / dpr
                                  + 2 * kEdgeMargin);
    return band;
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

QStringList ViewerWindow::overlayParts(const QList<OverlayField> &fields, int *nameIndex) const
{
    const QLocale locale;
    const bool hasImage = m_image.width > 0;
    QStringList parts;
    for (OverlayField field : fields) {
        switch (field) {
        case OverlayField::Name:
            if (nameIndex)
                *nameIndex = int(parts.size());
            parts << displayFileName(QFileInfo(m_image.path).fileName());
            break;
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
                parts << tr("%1 %").arg(zoomNumber(shownZoom(), locale));
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
        case OverlayField::Output:
            if (!m_renderer.output().description.isEmpty()) // none before the first frame
                parts << m_renderer.output().description;
            break;
        }
    }
    return parts;
}

void ViewerWindow::updateTopOverlay()
{
    if (!m_rendererReady)
        return;
    QStringList parts;
    int nameIndex = -1; // where the file name is among the parts shown
    // Hidden (the default in a window): nothing to paint or upload on every zoom step.
    if (!m_image.path.isEmpty() && topOverlayMode() != OverlayVisibility::Hidden)
        parts = overlayParts(m_settings.overlayFields, &nameIndex);
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
    const qreal paddingX = 14.0, paddingY = 6.0;
    const qreal maxText = std::max(60.0, width() - 2 * kEdgeMargin - 2 * paddingX);
    QString text = parts.join(kFieldSeparator);
    // Too wide: shorten the file name first (it is usually the longest part), then the line.
    if (metrics.horizontalAdvance(text) > maxText && nameIndex >= 0) {
        QStringList others = parts;
        others.removeAt(nameIndex);
        const qreal rest = metrics.horizontalAdvance(others.join(kFieldSeparator) + kFieldSeparator);
        parts[nameIndex] = metrics.elidedText(parts.at(nameIndex), Qt::ElideMiddle, std::max(60.0, maxText - rest));
    }
    for (QString &part : parts)
        part = isolated(part);
    text = metrics.elidedText(parts.join(kFieldSeparator), Qt::ElideRight, maxText);
    const QSizeF logical(std::ceil(metrics.horizontalAdvance(text) + 2 * paddingX),
                         std::ceil(metrics.height() + 2 * paddingY));

    QImage overlay((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setLayoutDirection(QGuiApplication::layoutDirection());
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_settings.panelBackground());
    painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), kPanelRadius, kPanelRadius);
    painter.setFont(font);
    const QRectF textRect(paddingX, paddingY, logical.width() - 2 * paddingX, metrics.height());
    drawPanelText(painter, textRect, Qt::AlignCenter, text, m_settings.panelText(Settings::kPanelValueGrey),
                  m_settings.overlayOutline);
    painter.end();

    m_topOverlaySize = overlay.size();
    m_renderer.setOverlay(Renderer::TopLayer, overlay);
    requestUpdate();
}

void ViewerWindow::placeOverlays(Renderer::Frame *frame) const
{
    const double dpr = devicePixelRatio();
    const double margin = kEdgeMargin * dpr;
    if (!m_gallerySize.isEmpty())
        frame->overlayRects[Renderer::GalleryLayer] = m_galleryRect;
    if (!m_messageSize.isEmpty())
        frame->overlayRects[Renderer::MessageLayer] =
            QRectF(QPointF(std::round((deviceSize().width() - m_messageSize.width()) / 2.0),
                           std::round((deviceSize().height() - m_messageSize.height()) / 2.0)),
                   QSizeF(m_messageSize));
    if (!m_overlaySize.isEmpty()) {
        const double x = QGuiApplication::layoutDirection() == Qt::RightToLeft
                             ? deviceSize().width() - margin - m_overlaySize.width()
                             : margin;
        frame->overlayRects[Renderer::InfoLayer] =
            QRectF(QPointF(std::round(x), std::round(deviceSize().height() - margin - galleryReserve() * dpr - m_overlaySize.height())),
                   QSizeF(m_overlaySize));
    }
    if (topOverlayVisible())
        frame->overlayRects[Renderer::TopLayer] =
            QRectF(QPointF(std::round((deviceSize().width() - m_topOverlaySize.width()) / 2.0), std::round(kEdgeMargin * dpr)),
                   QSizeF(m_topOverlaySize));
    frame->overlayRects[Renderer::SlideshowLayer] = slideshowToolbarRect();
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
    const double zoneWidth = sideZoneWidth() * dpr;
    const double centreX = zone == Zone::Previous ? zoneWidth / 2.0 : deviceSize().width() - zoneWidth / 2.0;
    // Whole device pixels: the button texture is sampled 1:1.
    return QRectF(QPointF(std::round(centreX - pixels / 2.0), std::round((deviceSize().height() - pixels) / 2.0)),
                  QSizeF(pixels, pixels));
}

void ViewerWindow::updateSlideshowToolbar()
{
    if (!m_rendererReady)
        return;
    if (!m_slideshow || m_toolbarHidden) {
        if (!m_toolbarSize.isEmpty()) {
            m_toolbarSize = QSize();
            m_toolbarKey.clear();
            m_renderer.setOverlay(Renderer::SlideshowLayer, QImage());
            requestUpdate();
        }
        return;
    }
    const double dpr = devicePixelRatio();
    const QColor background = m_settings.panelBackground();
    const int hover = int(m_toolbarHover) - 1, press = int(m_toolbarPress) - 1; // -1: none
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(m_slideshowPaused).arg(hover).arg(press).arg(dpr).arg(background.alpha());
    if (key == m_toolbarKey)
        return;
    m_toolbarKey = key;
    const QImage image = slideshowToolbar(m_slideshowPaused, hover, press, dpr, background);
    m_toolbarSize = image.size();
    m_renderer.setOverlay(Renderer::SlideshowLayer, image);
    requestUpdate();
}

QRectF ViewerWindow::slideshowToolbarRect() const
{
    if (m_toolbarSize.isEmpty())
        return {};
    // Centred below the top overlay when it shows, else at the top margin (D-60).
    const double dpr = devicePixelRatio();
    double top = kEdgeMargin * dpr;
    if (topOverlayVisible())
        top += m_topOverlaySize.height() + kEdgeMargin * dpr;
    return QRectF(QPointF(std::round((deviceSize().width() - m_toolbarSize.width()) / 2.0), std::round(top)),
                  QSizeF(m_toolbarSize));
}

ViewerWindow::ToolbarButton ViewerWindow::toolbarButtonAt(const QPointF &position) const
{
    const QRectF rect = slideshowToolbarRect();
    const QPointF device = position * devicePixelRatio();
    if (!rect.contains(device))
        return ToolbarButton::None;
    // Each button takes its half of the panel, padding included.
    return device.x() < rect.center().x() ? ToolbarButton::Stop : ToolbarButton::PlayPause;
}
