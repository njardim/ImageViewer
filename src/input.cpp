// ViewerWindow, part 8: the keyboard, the mouse and the wheel (D-08: split from viewer.cpp).
// Keys run commands (D-30); the mouse pans, clicks the side zones (D-31), the gallery (D-60) and
// the toolbars (D-65, D-112);
// the wheel zooms, or scrolls the gallery under the pointer. The side zones and the pointer's
// cursor and hiding live here too, and the color picker's work with the pointer (D-115): the
// pixel under it, a click that picks, its panel and the copies of its values.
#include "viewer.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QStyleHints>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

// What the color picker picks, reads from the file and copies (D-115): the tests read it.
Q_LOGGING_CATEGORY(lcPicker, "imageviewer.picker", QtWarningMsg)

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier && m_mainToolbar.menuOpen()) {
        closeToolbarMenu(); // the "…" panel first, as a menu
        return;
    }
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier && m_pickerOn) {
        togglePicker(); // the picker, a mode, is left first
        return;
    }
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier && (m_slideshow.running() || visibility() == QWindow::FullScreen)) {
        if (m_slideshow.running()) {
            stopSlideshow();
            showNotice(tr("Slideshow stopped"));
        }
        if (visibility() == QWindow::FullScreen)
            leaveFullScreen();
        return;
    }
    if (!executeShortcut(e))
        QWindow::keyPressEvent(e);
}

void ViewerWindow::mousePressEvent(QMouseEvent *e)
{
    // Only a bar on screen takes the press: a hidden one comes back with it (pointerActive).
    const bool inMenu = toolbarMenuRect().contains(e->position() * devicePixelRatio());
    const int menuRow = toolbarMenuAt(e->position());
    const int barButton = mainToolbarAt(e->position());
    const int slideshowButton = slideshowBarAt(e->position());
    const bool onGallery = galleryShown() && m_gallery.contains(e->position());
    const bool onPicker = overPicker(e->position());
    pointerActive();
    if (m_mainToolbar.menuOpen() && !inMenu && barButton != Toolbar::kMore) {
        closeToolbarMenu(); // a press outside the "…" panel closes it and does nothing else, as outside a menu
        return;
    }
    switch (e->button()) {
    case Qt::RightButton:
        showContextMenu(e->globalPosition().toPoint());
        return;
    case Qt::BackButton: // mouse side buttons: the commands, so they stop where the keys do
        run(Command::Previous);
        return;
    case Qt::ForwardButton:
        run(Command::Next);
        return;
    case Qt::LeftButton:
        break;
    default:
        return;
    }
    if (inMenu) {
        m_toolbarMenuPress = menuRow; // a click when released on the same row
        return;
    }
    if (barButton != -1) {
        m_mainToolbar.setPressed(barButton); // a click when released on the same button
        QToolTip::hideText();
        m_tooltipTimer.stop();
        updateMainToolbar();
        return;
    }
    if (slideshowButton != -1) {
        m_slideshowBar.setPressed(slideshowButton); // likewise
        QToolTip::hideText();
        m_tooltipTimer.stop();
        updateSlideshowToolbar();
        return;
    }
    if (onGallery) { // a thumbnail shows its image; the bar itself does nothing
        if (const int slot = m_gallery.slotAt(e->position()); slot >= 0)
            galleryClicked(slot);
        return;
    }
    if (onPicker) { // a value is copied when released on it; the panel itself does nothing
        m_pickerPress = pickerRowAt(e->position());
        return;
    }
    m_pressZone = zoneAt(e->position());
    m_pressMoved = false;
    m_dragOrigin = e->position();
    m_panOrigin = m_pan;
    m_dragging = canPan();
    m_pickPress = m_pickerOn; // a click picks; a drag still pans
    if (m_pressZone != Zone::None)
        updateNavigationButtons(); // pressed look
}

void ViewerWindow::mouseMoveEvent(QMouseEvent *e)
{
    pointerActive();
    m_pointer = e->position();
    m_pointerIn = true;
    if (m_pickerOn)
        requestUpdate(); // the loupe follows the pointer
    if (!(e->buttons() & Qt::LeftButton)) {
        // No press in progress (or its release went elsewhere, e.g. to the context menu).
        cancelPress();
        const bool inMenu = toolbarMenuRect().contains(e->position() * devicePixelRatio());
        if (m_mainToolbar.setMenuHover(toolbarMenuAt(e->position())))
            updateMainToolbar();
        const int barButton = mainToolbarAt(e->position());
        setMainToolbarHover(barButton);
        const int slideshowButton = slideshowBarAt(e->position());
        setSlideshowBarHover(slideshowButton);
        const bool overBar = inMenu || barButton != -1 || slideshowButton != -1;
        const int pickerRow = overBar ? -1 : pickerRowAt(e->position());
        if (m_picker.setHover(pickerRow)) {
            updatePicker();
            if (pickerRow >= 0)
                //: The color picker: the tooltip of a value, which a click copies.
                QToolTip::showText(mapToGlobal(e->position().toPoint()) + QPoint(12, 16), tr("Copy"));
            else
                QToolTip::hideText();
            updateCursor();
        }
        setHoverZone(overBar ? Zone::None : zoneAt(e->position()));
        setPointerAtTop(e->position().y() >= 0 && e->position().y() < topActivationHeight());
        m_gallery.setHover(galleryShown() && !overBar ? m_gallery.slotAt(e->position()) : -1);
        return;
    }
    if (m_pressZone != Zone::None && !m_pressMoved) {
        // A press on a side zone is a click until it moves beyond the platform's drag distance.
        if ((e->position() - m_dragOrigin).manhattanLength() < QGuiApplication::styleHints()->startDragDistance())
            return;
        m_pressMoved = true;
        updateNavigationButtons();
    }
    if (m_pickPress && (e->position() - m_dragOrigin).manhattanLength() >= QGuiApplication::styleHints()->startDragDistance())
        m_pickPress = false; // a drag pans, and picks nothing
    if (!m_dragging)
        return;
    m_pan = m_panOrigin + (e->position() - m_dragOrigin) * devicePixelRatio();
    clampPan();
    requestUpdate();
}

void ViewerWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (const int row = std::exchange(m_toolbarMenuPress, -1); row != -1) {
        if (toolbarMenuAt(e->position()) == row)
            toolbarButtonClicked(row);
        return;
    }
    if (const int row = std::exchange(m_pickerPress, -1); row != -1) {
        if (pickerRowAt(e->position()) == row)
            copyPickerValue(row);
        return;
    }
    if (const int pressed = m_mainToolbar.pressed(); pressed != -1) {
        m_mainToolbar.setPressed(-1);
        updateMainToolbar();
        if (mainToolbarAt(e->position()) == pressed)
            toolbarButtonClicked(pressed);
        return;
    }
    if (const int pressed = m_slideshowBar.pressed(); pressed != -1) {
        m_slideshowBar.setPressed(-1);
        updateSlideshowToolbar();
        if (slideshowBarAt(e->position()) == pressed)
            toolbarButtonClicked(pressed);
        return;
    }
    m_dragging = false;
    if (std::exchange(m_pickPress, false))
        pickAt(e->position());
    const Zone pressed = std::exchange(m_pressZone, Zone::None);
    const Zone released = zoneAt(e->position());
    if (pressed != Zone::None && !m_pressMoved && released == pressed)
        run(pressed == Zone::Next ? Command::Next : Command::Previous);
    // Refresh unconditionally: the button loses its pressed look even when the zone is unchanged.
    m_hoverZone = released;
    updateCursor();
    updateNavigationButtons();
}

void ViewerWindow::cancelPress()
{
    // Nothing is clicked, and the pressed look goes.
    m_dragging = false;
    m_pickPress = false;
    m_pickerPress = -1;
    if (std::exchange(m_pressZone, Zone::None) != Zone::None)
        updateNavigationButtons();
    m_toolbarMenuPress = -1;
    if (m_slideshowBar.setPressed(-1))
        updateSlideshowToolbar();
    if (m_mainToolbar.setPressed(-1))
        updateMainToolbar();
}

void ViewerWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || (galleryShown() && m_gallery.contains(e->position())))
        return;
    if (slideshowBarAt(e->position()) != -1 || mainToolbarAt(e->position()) != -1
        || toolbarMenuRect().contains(e->position() * devicePixelRatio())) {
        mousePressEvent(e); // quick clicks on its buttons are clicks, never full screen
        return;
    }
    if (zoneAt(e->position()) != Zone::None || overPicker(e->position())) {
        mousePressEvent(e); // quick clicks on a side keep navigating; on the picker's values, copying
        return;
    }
    if (m_pickerOn) {
        mousePressEvent(e); // while picking, the second click of a pair picks again
        return;
    }
    // The first click may have reached the end of a folder that does not loop: the second
    // click of the pair is still a click on the side, never a request for full screen.
    if (inSideStrip(e->position()))
        return;
    run(Command::FullScreen);
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    // Scroll gestures (trackpads: they have phases or a touchpad device) pan; wheels and
    // Ctrl+scroll zoom. pixelDelta alone says nothing: macOS sets it for mouse wheels too.
    const bool onGallery = galleryShown() && m_gallery.contains(e->position());
    const bool onBar = slideshowBarAt(e->position()) != -1 || mainToolbarAt(e->position()) != -1
                       || toolbarMenuRect().contains(e->position() * devicePixelRatio()) || overPicker(e->position());
    pointerActive();
    if (onBar)
        return;
    if (onGallery) { // over the gallery the wheel scrolls the thumbnails
        if (const int steps = m_gallery.wheel(e->angleDelta().y() + e->angleDelta().x()); steps != 0)
            m_gallery.scroll(-steps);
        return;
    }
    const QPointingDevice *device = e->pointingDevice();
    const bool gesture = e->phase() != Qt::NoScrollPhase
                         || (device && device->type() == QInputDevice::DeviceType::TouchPad);
    if (gesture && !(e->modifiers() & Qt::ControlModifier)) {
        if (canPan()) {
            const QPointF delta = !e->pixelDelta().isNull() ? QPointF(e->pixelDelta())
                                                            : QPointF(e->angleDelta()) / 8.0; // degrees ~ pixels
            m_pan += delta * devicePixelRatio();
            clampPan();
            requestUpdate();
        }
        return;
    }
    const double steps = e->angleDelta().y() / 120.0;
    if (steps != 0.0)
        zoomAt(std::pow(kZoomStep, steps), e->position() * devicePixelRatio());
}

double ViewerWindow::sideZoneWidth() const
{
    if (!m_settings.sideZones || m_files.size() < 2)
        return 0.0;
    // Narrow windows keep a middle for panning and double-clicks.
    return std::min<double>(m_settings.sideZoneWidth, width() / 4.0);
}

bool ViewerWindow::inSideStrip(const QPointF &position) const
{
    const double zone = sideZoneWidth();
    return zone > 0.0 && position.y() >= 0 && position.y() < height()
           && ((position.x() >= 0 && position.x() < zone) || (position.x() < width() && position.x() >= width() - zone));
}

ViewerWindow::Zone ViewerWindow::zoneAt(const QPointF &position) const
{
    if (m_pickerOn || !inSideStrip(position)) // while picking, a click picks (D-115)
        return Zone::None;
    if (position.x() < sideZoneWidth())
        return hasNeighbour(-1) ? Zone::Previous : Zone::None;
    return hasNeighbour(+1) ? Zone::Next : Zone::None;
}

void ViewerWindow::setHoverZone(Zone zone)
{
    if (zone == m_hoverZone)
        return;
    m_hoverZone = zone;
    updateCursor();
    updateNavigationButtons();
}

void ViewerWindow::pointerActive()
{
    if (m_pointerHidden) {
        m_pointerHidden = false;
        updateCursor();
    }
    if (m_settings.pointerAutoHide && !m_pickerOn) // never while picking (D-115)
        m_pointerTimer.start(m_settings.autoHideMs); // the one delay (D-113)
    else
        m_pointerTimer.stop();
    // Everything that hides by itself shows again, and waits for the pointer to stay still.
    m_toolbarHide.pointerMoved();
    m_mainToolbarHide.pointerMoved();
    m_galleryHide.pointerMoved();
    m_overlayHide.pointerMoved();
    m_panelHide.pointerMoved();
}

void ViewerWindow::updateCursor()
{
    const bool onButton = m_hoverZone != Zone::None || m_slideshowBar.hover() != -1 || m_mainToolbar.hover() != -1
                          || m_mainToolbar.menuHover() != -1 || m_picker.hover() != -1;
    // While picking, a cross over the image, its centre the pixel a click picks.
    const bool picking = m_pickerOn && m_pointerIn && texelAt(m_pointer).has_value() && !overPicker(m_pointer)
                         && !(galleryShown() && m_gallery.contains(m_pointer));
    setCursor(m_pointerHidden ? Qt::BlankCursor
              : onButton      ? Qt::PointingHandCursor
              : picking       ? Qt::CrossCursor
                              : Qt::ArrowCursor);
}

void ViewerWindow::togglePicker()
{
    m_pickerOn = !m_pickerOn;
    m_pickPress = false;
    m_pickerPress = -1;
    if (m_pickerOn) { // the side zones rest, and the pointer stays
        setHoverZone(Zone::None);
        m_pointerTimer.stop();
        m_pointerHidden = false;
        // The picker reads the pixels, which the window does not keep (navigation.cpp): the cache's
        // copy, or the file decoded again.
        if (m_image.width > 0 && m_image.error.isEmpty() && !m_image.pixels) { // isValid() needs the pixels
            const std::optional<Image> kept = m_cache.find(m_image.path, m_shownLimit);
            if (kept && kept->pixels && kept->width == m_image.width && kept->height == m_image.height) {
                m_image.pixels = kept->pixels;
            } else {
                m_imageStale = true;
                scheduleWork();
            }
        }
    } else {
        m_picker.setHover(-1);
        m_image.pixels.reset(); // the cache, or the GPU alone, keeps them
        m_fileSamples.reset();
        pointerActive();
    }
    updateCursor();
    updateOverlay(); // the panel, and the information layer's room for it
}

ColorPicker::Look ViewerWindow::pickerLook() const
{
    ColorPicker::Look look;
    look.devicePixelRatio = devicePixelRatio();
    look.panel = m_settings.panelBackground();
    look.ink = m_settings.panelText(Settings::kPanelValueGrey);
    look.label = m_settings.panelText(Settings::kPanelLabelGrey);
    look.hover = m_settings.highlightTile(true); // the gallery's hovered tile (D-114)
    look.font = QGuiApplication::font();
    look.radius = kPanelRadius;
    return look;
}

ColorPicker::Source ViewerWindow::pickerSource() const
{
    ColorPicker::Source source;
    source.colour = m_image.colour;
    source.cmyk = m_image.sourceCmyk;
    source.gray = m_image.sourceGray;
    source.isFloat = m_image.sourceFloat;
    source.bits = m_image.sourceBits;
    source.hasAlpha = m_image.hasAlpha;
    source.hdr = m_image.colour.isHdr() || m_image.maxComponent > 1.0f;
    source.firstFrame = m_frameIndex == 0 || !m_animation;
    return source;
}

void ViewerWindow::updatePicker()
{
    if (!m_rendererReady)
        return;
    m_picker.setLook(pickerLook());
    const QImage panel = m_pickerOn && m_image.width > 0 ? m_picker.texture() : QImage();
    if (const qint64 key = panel.isNull() ? 0 : panel.cacheKey(); key != m_pickerTexture) {
        if (!panel.isNull() && (m_pickerTexture == 0 || panel.size() != m_pickerTextureSize))
            qCInfo(lcPicker) << "picker panel" << int(m_picker.size().height()) << "px";
        m_pickerTexture = key;
        m_pickerTextureSize = panel.size();
        m_renderer.setOverlay(Renderer::PickerLayer, panel);
    }
    requestUpdate();
}

QRectF ViewerWindow::pickerRect() const
{
    if (!m_pickerOn || m_image.width <= 0 || m_pickerTexture == 0)
        return {};
    // Above the information panel and its notices' place (overlays.cpp), at the window's left edge.
    const double dpr = devicePixelRatio();
    const double margin = kEdgeMargin * dpr;
    const QSizeF size((m_picker.size() * dpr).toSize());
    const double x = QGuiApplication::layoutDirection() == Qt::RightToLeft ? deviceSize().width() - margin - size.width()
                                                                          : margin;
    const double below = (galleryShown() ? m_gallery.reserve() : 0.0) + m_pickerOffset;
    return QRectF(QPointF(std::round(x), std::round(deviceSize().height() - margin - below * dpr - size.height())), size);
}

bool ViewerWindow::overPicker(const QPointF &position) const
{
    return pickerRect().contains(position * devicePixelRatio());
}

int ViewerWindow::pickerRowAt(const QPointF &position) const
{
    const QRectF panel = pickerRect();
    const double dpr = devicePixelRatio();
    if (!panel.contains(position * dpr))
        return -1;
    return m_picker.rowAt((position * dpr - panel.topLeft()) / dpr);
}

std::optional<QPoint> ViewerWindow::texelAt(const QPointF &position) const
{
    if (m_image.width <= 0)
        return std::nullopt;
    Renderer::Frame frame;
    frame.imageRect = imageRect();
    frame.quarterTurns = m_quarterTurns;
    frame.mirrored = m_mirrored;
    const QPointF uv = Renderer::textureCoordinates(frame, position * devicePixelRatio());
    if (!(uv.x() >= 0.0 && uv.y() >= 0.0 && uv.x() < 1.0 && uv.y() < 1.0))
        return std::nullopt;
    return QPoint(std::min(int(uv.x() * m_image.width), m_image.width - 1),
                  std::min(int(uv.y() * m_image.height), m_image.height - 1));
}

void ViewerWindow::pickAt(const QPointF &position)
{
    const std::optional<QPoint> texel = texelAt(position);
    const PixelBuffer pixels = m_framePixels ? m_framePixels : m_image.pixels; // the frame on screen
    const std::size_t count = std::size_t(m_image.width) * std::size_t(m_image.height);
    if (!texel || !pixels || pixels->size() < count * 4)
        return; // outside the image: nothing to pick
    const qfloat16 *p = pixels->data() + (std::size_t(texel->y()) * std::size_t(m_image.width) + std::size_t(texel->x())) * 4;
    ColorPicker::Sample sample;
    sample.alpha = float(p[3]);
    for (int c = 0; c < 3; ++c)
        sample.rgb[c] = sample.alpha > 0.0f ? float(p[c]) / sample.alpha : 0.0f;
    // In the file's own pixels: the texture may be smaller (reduced for the GPU) or an SVG drawn at a scale.
    const double scale = sourceScale();
    sample.position = QPoint(int((texel->x() + 0.5) * scale), int((texel->y() + 0.5) * scale));
    m_picker.pick(sample, pickerSource(), QLocale());
    m_pickPath = m_image.path;
    QStringList values;
    for (const ColorPicker::Row &row : m_picker.rows())
        values << row.label + QLatin1Char('=') + row.copy;
    qCInfo(lcPicker).noquote() << "picked" << QFileInfo(m_image.path).fileName() << values.join(QStringLiteral("; "));
    fillFileValue();
    updateOverlay(); // the panel grows with its values
}

void ViewerWindow::fillFileValue()
{
    if (!m_picker.waitsForFileValue())
        return;
    if (m_fileSamples && m_fileSamples->path() == m_pickPath) {
        m_picker.setFileValue({m_fileSamples->at(m_picker.picked()->position), m_fileSamples->bits(),
                               m_fileSamples->isFloat()});
        for (const ColorPicker::Row &row : m_picker.rows())
            if (row.label == QCoreApplication::translate("ColorPicker", "File"))
                qCInfo(lcPicker).noquote() << "file value:" << row.text;
        updateOverlay();
        return;
    }
    if (m_samplesBusy)
        return; // fileSamplesRead() fills it
    // The file read again, once for as long as it shows: its own samples hold the exact values.
    m_samplesBusy = true;
    m_samplesPath = m_pickPath;
    m_samplesWatcher.setFuture(QtConcurrent::run(&m_samplesPool, [path = m_samplesPath] { return FileSamples::read(path); }));
}

void ViewerWindow::fileSamplesRead()
{
    m_samplesBusy = false;
    std::shared_ptr<const FileSamples> samples = m_samplesWatcher.result();
    if (!m_pickerOn)
        return;
    if (m_samplesPath != m_pickPath) {
        fillFileValue(); // a pixel of another file was picked meanwhile: its own
        return;
    }
    if (!samples) { // the panel says so, and the next pick tries again
        if (m_picker.waitsForFileValue() && m_picker.setFileValue({}))
            updateOverlay();
        return;
    }
    m_fileSamples = std::move(samples);
    fillFileValue();
}

void ViewerWindow::placePicker(Renderer::Frame *frame) const
{
    const QRectF panel = pickerRect();
    if (panel.isEmpty())
        return;
    const double dpr = devicePixelRatio();
    frame->overlayRects[Renderer::PickerLayer] = panel;
    const auto place = [&](const QRectF &r) {
        return QRectF(panel.topLeft() + QPointF(std::round(r.left() * dpr), std::round(r.top() * dpr)),
                      QSizeF(std::round(r.width() * dpr), std::round(r.height() * dpr)));
    };
    frame->loupeRect = place(m_picker.loupeRect());
    frame->currentRect = place(m_picker.currentRect());
    frame->pickedRect = place(m_picker.pickedRect());
    frame->loupeCells = ColorPicker::kCells;
    frame->pickerCheck = float(std::round(5.0 * dpr));
    // The texture's pixel under the pointer, also beyond the image's edge (the loupe shows the edge);
    // away from the window, none of it.
    QPoint texel(-1000000, -1000000);
    if (m_pointerIn && m_image.width > 0 && !frame->imageRect.isEmpty()) { // as drawn: during a glide too
        const QPointF uv = Renderer::textureCoordinates(*frame, m_pointer * dpr);
        texel = QPoint(int(std::floor(uv.x() * m_image.width)), int(std::floor(uv.y() * m_image.height)));
    }
    frame->loupeTexel = texel;
    if (const std::optional<ColorPicker::Sample> &picked = m_picker.picked()) {
        for (int c = 0; c < 3; ++c)
            frame->pickedColour[c] = picked->rgb[c] * picked->alpha;
        frame->pickedColour[3] = picked->alpha;
    }
}

void ViewerWindow::copyPickerValue(int row)
{
    if (row < 0 || row >= m_picker.rows().size())
        return;
    const QString text = m_picker.rows().at(row).copy;
    QGuiApplication::clipboard()->setText(text);
    qCInfo(lcPicker).noquote() << "copied:" << text;
    //: A notice: a color picker's value was copied, e.g. "Copied: #F79134".
    showNotice(tr("Copied: %1").arg(text));
}
