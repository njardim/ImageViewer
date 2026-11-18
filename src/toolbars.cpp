// ViewerWindow, part 9: the toolbars over the image (D-08: gathered from overlays.cpp and
// playback.cpp). The toolbar at the top (D-87, D-112): the commands the user chose, as buttons
// (Toolbar), each run through run() as a key's (D-85); the slideshow's toolbar (D-65): stop, and
// pause or play; and the hiding of those and of the gallery while the pointer is still (AutoHide).
// At the top, the toolbar first, then the top overlay, then the slideshow's toolbar, which shows
// only while the toolbar does not hold the slideshow's buttons: never two controls for one thing
// at once (the owner, 2026-11-18).
#include "viewer.h"

#include "openwith.h"

#include <QApplication>
#include <QCursor>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <cmath>

namespace {

constexpr double kToolbarButton = 44.0;  // the slideshow toolbar's round buttons, logical pixels (D-65)
constexpr double kToolbarPadding = 8.0;  // around and between them
constexpr int kSlideshowPause = 1000;    // the toolbar's id of the slideshow's own pause or play
constexpr int kTooltipDelayMs = 700;     // as Qt's widgets wait before a tooltip

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

} // namespace

std::optional<Glyph> ViewerWindow::glyphFor(Command command)
{
    switch (command) {
    case Command::Open: return Glyph::Open;
    case Command::ShowInFolder: return Glyph::ShowInFolder;
    case Command::OpenWith: return Glyph::OpenWith;
    case Command::CopyImage: return Glyph::CopyImage;
    case Command::CopyPath: return Glyph::CopyPath;
    case Command::Rename: return Glyph::Rename;
    case Command::MoveToTrash: return Glyph::MoveToTrash;
    case Command::DeletePermanently: return Glyph::DeletePermanently;
    case Command::UndoTrash: return Glyph::UndoTrash;
    case Command::Settings: return Glyph::Settings;
    case Command::Quit: return Glyph::Quit;
    case Command::Previous: return Glyph::Previous;
    case Command::Next: return Glyph::Next;
    case Command::First: return Glyph::First;
    case Command::Last: return Glyph::Last;
    case Command::ZoomIn: return Glyph::ZoomIn;
    case Command::ZoomOut: return Glyph::ZoomOut;
    case Command::Fit: return Glyph::Fit;
    case Command::FitWidth: return Glyph::FitWidth;
    case Command::FitHeight: return Glyph::FitHeight;
    case Command::Fill: return Glyph::Fill;
    case Command::ActualSize: return Glyph::ActualSize;
    case Command::LockZoom: return Glyph::LockZoom;
    case Command::FullScreen: return Glyph::FullScreen;
    case Command::Minimize: return Glyph::Minimize;
    case Command::Info: return Glyph::Info;
    case Command::InfoOverlay: return Glyph::InfoOverlay;
    case Command::Checkerboard: return Glyph::Checkerboard;
    case Command::Gallery: return Glyph::Gallery;
    case Command::RotateClockwise: return Glyph::RotateClockwise;
    case Command::RotateCounterclockwise: return Glyph::RotateCounterclockwise;
    case Command::FlipHorizontal: return Glyph::FlipHorizontal;
    case Command::FlipVertical: return Glyph::FlipVertical;
    case Command::ExposureUp: return Glyph::ExposureUp;
    case Command::ExposureDown: return Glyph::ExposureDown;
    case Command::ExposureReset: return Glyph::ExposureReset;
    case Command::ToneMap: return Glyph::ToneMap;
    case Command::ClipWarning: return Glyph::ClipWarning;
    case Command::PlayPause: return Glyph::PlayPause;
    case Command::PreviousFrame: return Glyph::PreviousFrame;
    case Command::NextFrame: return Glyph::NextFrame;
    case Command::Slideshow: return Glyph::Slideshow;
    // A submenu's own item, the toolbar itself, the Help menu's: not buttons.
    case Command::ClearRecent:
    case Command::OpenWithOther:
    case Command::Toolbar:
    case Command::About:
    case Command::AboutQt: break;
    }
    return std::nullopt;
}

QString ViewerWindow::buttonName(Command command) const
{
    // The menu's name (the owner: one name for a command wherever it shows), without the "…" that
    // says a menu item opens a dialog.
    QString name = commandText(command);
    if (name.endsWith(QChar(0x2026)))
        name.chop(1);
    return name;
}

QList<Toolbar::Button> ViewerWindow::mainToolbarButtons() const
{
    const QMetaEnum names = QMetaEnum::fromType<Command>();
    QList<Toolbar::Button> buttons;
    for (const QString &key : m_settings.toolbarButtons) {
        if (key == Settings::kToolbarSeparator) {
            Toolbar::Button separator;
            separator.separator = true;
            buttons.append(separator);
            continue;
        }
        bool known = false;
        const auto command = Command(names.keyToValue(key.toLatin1().constData(), &known));
        const std::optional<Glyph> glyph = known ? glyphFor(command) : std::nullopt;
        if (!glyph)
            continue; // a command of another version, or none
        if (command == Command::Slideshow && m_slideshow.running()) {
            // While it runs: stop, then pause or play, as on the slideshow's own toolbar (the owner).
            //: The toolbar's button that ends the running slideshow.
            buttons.append({int(Command::Slideshow), Glyph::Stop, tr("Stop")});
            const bool paused = m_slideshow.paused();
            //: The toolbar's button that resumes the paused slideshow.
            buttons.append({kSlideshowPause, paused ? Glyph::Play : Glyph::Pause,
                            paused ? tr("Play") : buttonName(Command::PlayPause)});
            continue;
        }
        bool checkable = false;
        const bool checked = isCommandChecked(command, &checkable);
        buttons.append({int(command), *glyph, buttonName(command), isCommandEnabled(command), checkable && checked});
    }
    return buttons;
}

void ViewerWindow::updateMainToolbar()
{
    if (!m_rendererReady)
        return;
    if (m_settings.showToolbar) {
        Toolbar::Look look;
        look.devicePixelRatio = devicePixelRatio();
        look.names = m_settings.toolbarNames;
        look.panel = m_settings.panelBackground();
        look.highlight = m_settings.highlight;
        look.font = QGuiApplication::font(); // the panels' own, a little smaller under the glyphs
        if (look.font.pointSizeF() > 0)
            look.font.setPointSizeF(look.font.pointSizeF() * 0.85);
        else
            look.font.setPixelSize(std::max(8, int(std::lround(look.font.pixelSize() * 0.85))));
        look.radius = kPanelRadius;
        m_mainToolbar.setLook(look);
        m_mainToolbar.setButtons(mainToolbarButtons());
        m_mainToolbar.layout(width() - 2 * kEdgeMargin);
    }
    const QImage image = m_settings.showToolbar && !m_mainToolbarHide.hidden() ? m_mainToolbar.texture() : QImage();
    const qint64 key = image.isNull() ? 0 : image.cacheKey();
    if (key != m_mainToolbarTexture) {
        m_mainToolbarTexture = key;
        m_renderer.setOverlay(Renderer::ToolbarLayer, image);
        requestUpdate();
    }
    if (const bool inToolbar = slideshowInMainToolbar(); inToolbar != m_slideshowInMainToolbar) {
        m_slideshowInMainToolbar = inToolbar;
        updateSlideshowToolbar();
    }
}

QRectF ViewerWindow::mainToolbarRect() const
{
    if (!m_settings.showToolbar || m_mainToolbarHide.hidden() || m_mainToolbar.size().isEmpty())
        return {};
    // Centred at the top margin (D-60), in whole device pixels: its texture is sampled 1:1.
    const double dpr = devicePixelRatio();
    const QSizeF size((m_mainToolbar.size() * dpr).toSize());
    return QRectF(QPointF(std::round((deviceSize().width() - size.width()) / 2.0), std::round(kEdgeMargin * dpr)), size);
}

double ViewerWindow::mainToolbarReserve() const
{
    // Its place stays while it hides by itself: nothing below it moves because the pointer stopped.
    return m_settings.showToolbar && !m_mainToolbar.size().isEmpty() ? m_mainToolbar.size().height() + kEdgeMargin : 0.0;
}

int ViewerWindow::mainToolbarAt(const QPointF &position) const
{
    const QRectF rect = mainToolbarRect();
    if (rect.isEmpty())
        return -1;
    const double dpr = devicePixelRatio();
    return m_mainToolbar.buttonAt((position * dpr - rect.topLeft()) / dpr);
}

void ViewerWindow::setMainToolbarHover(int id)
{
    if (!m_mainToolbar.setHover(id))
        return;
    QToolTip::hideText();
    if (id != -1)
        m_tooltipTimer.start(kTooltipDelayMs);
    else
        m_tooltipTimer.stop();
    updateCursor();
    updateMainToolbar();
}

void ViewerWindow::showToolbarTooltip()
{
    const int id = m_mainToolbar.hover();
    const Toolbar::Button *button = m_mainToolbar.button(id);
    const QRectF rect = mainToolbarRect();
    if (!button || rect.isEmpty() || QApplication::activePopupWidget())
        return;
    //: The toolbar's "…" button, which holds the buttons a narrow window cannot show.
    QString text = id == Toolbar::kMore ? tr("More") : button->name;
    const Command command = id == kSlideshowPause ? Command::PlayPause : Command(std::max(id, 0));
    if (const QList<QKeySequence> keys = shortcutsFor(command); id != Toolbar::kMore && !keys.isEmpty())
        //: A toolbar button's tooltip: %1 its name, %2 its keyboard shortcut, e.g. "Gallery (G)".
        text = tr("%1 (%2)").arg(text, keys.first().toString(QKeySequence::NativeText));
    const QRectF square = m_mainToolbar.buttonRect(id);
    const double dpr = devicePixelRatio();
    QToolTip::showText(mapToGlobal(QPointF(rect.left() / dpr + square.left(), rect.bottom() / dpr + 4).toPoint()), text);
}

void ViewerWindow::mainToolbarClicked(int id)
{
    QToolTip::hideText();
    m_tooltipTimer.stop();
    if (id == Toolbar::kMore) {
        showMainToolbarMenu();
    } else if (id == kSlideshowPause) {
        run(Command::PlayPause);
    } else if (Command(id) == Command::OpenWith) { // its menu under the button
        if (isCommandEnabled(Command::OpenWith)) {
            const QRectF rect = mainToolbarRect();
            const QRectF button = m_mainToolbar.buttonRect(id);
            const double dpr = devicePixelRatio();
            showOpenWithMenu(mapToGlobal(QPointF(rect.left() / dpr + button.left(), rect.bottom() / dpr + 2).toPoint()));
        }
    } else if (id >= 0) {
        run(Command(id));
    }
}

void ViewerWindow::showMainToolbarMenu()
{
    // The buttons a narrow window cannot hold, as the context menu names them (D-30).
    QMenu menu;
    const QColor text = menu.palette().color(QPalette::Text);
    for (const Toolbar::Button &button : m_mainToolbar.overflow()) {
        const Command command = button.id == kSlideshowPause ? Command::PlayPause : Command(button.id);
        QAction *action = addCommand(&menu, command);
        action->setIcon(glyphIcon(button.glyph, text));
    }
    const QRectF rect = mainToolbarRect();
    const QRectF more = m_mainToolbar.buttonRect(Toolbar::kMore);
    const double dpr = devicePixelRatio();
    makeTransient(menu, this);
    cancelPress();
    m_menuChoice = nullptr;
    menu.exec(mapToGlobal(QPointF(rect.left() / dpr + more.left(), rect.bottom() / dpr + 2).toPoint()));
    if (std::function<void()> choice = std::exchange(m_menuChoice, nullptr)) // once the menu is gone (D-85)
        QMetaObject::invokeMethod(this, std::move(choice), Qt::QueuedConnection);
}

void ViewerWindow::fillOpenWithMenu(QMenu *menu)
{
    // The applications are looked up when the menu opens (a scan of the system's registry).
    const QString file = m_image.path;
    const QList<OpenWithApp> apps = openWithApps(file);
    for (const OpenWithApp &app : apps) {
        QString label = app.name;
        label.replace(QLatin1Char('&'), QStringLiteral("&&")); // not a mnemonic
        connect(menu->addAction(label), &QAction::triggered, this, [this, app, file] {
            m_menuChoice = [this, app, file] {
                if (!openWith(app, file))
                    showNotice(tr("Cannot start “%1”.").arg(app.name));
            };
        });
    }
    if (apps.isEmpty())
        menu->addAction(tr("No applications found"))->setEnabled(false);
    menu->addSeparator();
    addCommand(menu, Command::OpenWithOther);
}

void ViewerWindow::showOpenWithMenu(const QPoint &globalPos)
{
    QMenu menu;
    fillOpenWithMenu(&menu);
    makeTransient(menu, this);
    cancelPress();
    m_menuChoice = nullptr;
    menu.exec(globalPos);
    if (std::function<void()> choice = std::exchange(m_menuChoice, nullptr))
        QMetaObject::invokeMethod(this, std::move(choice), Qt::QueuedConnection);
}

void ViewerWindow::toggleMainToolbar()
{
    savePreference([on = !m_settings.showToolbar](Settings &s) { s.showToolbar = on; });
    setMainToolbarHover(-1);
    m_mainToolbarHide.show();
    updateMainToolbar();
    updateOverlay(); // the overlay and the slideshow's toolbar move down below it, or back up
    requestUpdate();
}

bool ViewerWindow::slideshowInMainToolbar() const
{
    // The slideshow's button (start, or stop while it runs) on the toolbar, not under its "…":
    // whether the toolbar shows at this moment or hides by itself, so that the two never take
    // turns as the pointer moves and stops.
    return m_settings.showToolbar && m_mainToolbar.button(int(Command::Slideshow)) != nullptr;
}

bool ViewerWindow::galleryShown() const
{
    return m_gallery.visible() && !m_galleryHide.hidden();
}

void ViewerWindow::configureAutoHide()
{
    m_toolbarHide.configure(m_settings.slideshowToolbarAutoHide, m_settings.slideshowToolbarHideMs);
    m_mainToolbarHide.configure(m_settings.toolbarAutoHide, m_settings.toolbarHideMs);
    m_galleryHide.configure(m_settings.galleryAutoHide, m_settings.galleryHideMs);
}

void ViewerWindow::updateSlideshowToolbar()
{
    if (!m_rendererReady)
        return;
    if (!m_slideshow.running() || m_toolbarHide.hidden() || m_slideshowInMainToolbar) {
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
                            .arg(m_slideshow.paused()).arg(hover).arg(press).arg(dpr).arg(background.alpha());
    if (key == m_toolbarKey)
        return;
    m_toolbarKey = key;
    const QImage image = slideshowToolbar(m_slideshow.paused(), hover, press, dpr, background);
    m_toolbarSize = image.size();
    m_renderer.setOverlay(Renderer::SlideshowLayer, image);
    requestUpdate();
}

QRectF ViewerWindow::slideshowToolbarRect() const
{
    if (m_toolbarSize.isEmpty())
        return {};
    // Centred below the toolbar and the top overlay where they show, else at the top margin (D-60).
    const double dpr = devicePixelRatio();
    double top = (kEdgeMargin + mainToolbarReserve()) * dpr;
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

void ViewerWindow::showSlideshowToolbar()
{
    if (!m_slideshow.running())
        return;
    m_toolbarHide.show();
    updateSlideshowToolbar();
}

void ViewerWindow::setToolbarHover(ToolbarButton button)
{
    if (button == m_toolbarHover)
        return;
    m_toolbarHover = button;
    updateCursor();
    updateSlideshowToolbar();
}

void ViewerWindow::toolbarClicked(ToolbarButton button)
{
    // The toolbar shows only during a slideshow: Space and S, as the keys give them.
    if (button == ToolbarButton::PlayPause)
        run(Command::PlayPause);
    else if (button == ToolbarButton::Stop)
        run(Command::Slideshow);
}
