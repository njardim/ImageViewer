// ViewerWindow, part 9: the toolbars over the image (D-08: gathered from overlays.cpp and
// playback.cpp). The toolbar at the top (D-87, D-112): the commands the user chose, as buttons, each
// run through run() as a key's (D-85), and the "…" panel of what a narrow window cannot hold
// (D-113); the slideshow's toolbar (D-65): stop, then pause or play, in the same look, a Toolbar of
// its own (the owner, D-113); and the hiding of the bars, the gallery, the top overlay and the
// information panel while the pointer is still (AutoHide), after the one delay of the settings. At
// the top, the toolbar first, then the top overlay, then the slideshow's toolbar, which shows only
// while the toolbar does not hold the slideshow's buttons: never two controls for one thing at once
// (the owner, D-112). What hides by itself gives its place to what is below or above it, as when it
// is switched off (the owner, D-113).
#include "viewer.h"

#include "openwith.h"

#include <QApplication>
#include <QCursor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kSlideshowPause = 1000; // the id of the slideshow's own pause or play, on either bar
constexpr int kTooltipDelayMs = 700;  // as Qt's widgets wait before a tooltip
constexpr double kTipGap = 8.0;       // between a tooltip and what it names, or the pointer (the owner, D-116)
constexpr double kPointerBody = 16.0; // below the pointer's hot spot, its arrow or hand: a tooltip goes under it
constexpr double kMenuGap = 6.0;      // between the toolbar and its "…" panel, logical pixels

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
    case Command::ColorPicker: return Glyph::ColorPicker;
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

Toolbar::Look ViewerWindow::toolbarLook() const
{
    Toolbar::Look look;
    look.devicePixelRatio = devicePixelRatio();
    look.names = m_settings.toolbarNames;
    look.panel = m_settings.panelBackground();
    look.ink = m_settings.panelText(Settings::kPanelValueGrey);
    look.on = m_settings.highlightTile(false); // as the gallery's current thumbnail and hovered one (D-114)
    look.hover = m_settings.highlightTile(true);
    look.menuFont = QGuiApplication::font(); // the panels' own; the names a little smaller
    look.font = look.menuFont;
    if (look.font.pointSizeF() > 0)
        look.font.setPointSizeF(look.font.pointSizeF() * 0.85);
    else
        look.font.setPixelSize(std::max(8, int(std::lround(look.font.pixelSize() * 0.85))));
    look.radius = kPanelRadius;
    return look;
}

QList<Toolbar::Button> ViewerWindow::mainToolbarButtons() const
{
    const QMetaEnum names = QMetaEnum::fromType<Command>();
    const auto key = [this](Command command) {
        const QList<QKeySequence> keys = shortcutsFor(command);
        return keys.isEmpty() ? QString() : keys.first().toString(QKeySequence::NativeText);
    };
    QList<Toolbar::Button> buttons;
    for (const QString &name : m_settings.toolbarButtons) {
        if (name == Settings::kToolbarSeparator) {
            Toolbar::Button separator;
            separator.separator = true;
            buttons.append(separator);
            continue;
        }
        bool known = false;
        const auto command = Command(names.keyToValue(name.toLatin1().constData(), &known));
        const std::optional<Glyph> glyph = known ? glyphFor(command) : std::nullopt;
        if (!glyph)
            continue; // a command of another version, or none
        if (command == Command::Slideshow && m_slideshow.running()) {
            for (Toolbar::Button button : slideshowBarButtons()) // stop, then pause or play (the owner)
                buttons.append(button);
            continue;
        }
        bool checkable = false;
        const bool checked = isCommandChecked(command, &checkable);
        // Settings and Quit, the last group, stay on the bar when "…" takes others (the owner, D-115).
        buttons.append({int(command), *glyph, buttonName(command), isCommandEnabled(command), checkable && checked,
                        false, key(command), command == Command::Settings || command == Command::Quit});
    }
    return buttons;
}

QList<Toolbar::Button> ViewerWindow::slideshowBarButtons() const
{
    const auto key = [this](Command command) {
        const QList<QKeySequence> keys = shortcutsFor(command);
        return keys.isEmpty() ? QString() : keys.first().toString(QKeySequence::NativeText);
    };
    const bool paused = m_slideshow.paused();
    //: The toolbar's button that ends the running slideshow.
    return {{int(Command::Slideshow), Glyph::Stop, tr("Stop"), true, false, false, key(Command::Slideshow)},
            //: The toolbar's button that resumes the paused slideshow.
            {kSlideshowPause, paused ? Glyph::Play : Glyph::Pause, paused ? tr("Play") : buttonName(Command::PlayPause),
             true, false, false, key(Command::PlayPause)}};
}

void ViewerWindow::updateMainToolbar()
{
    if (!m_rendererReady)
        return;
    if (m_settings.showToolbar) {
        m_mainToolbar.setLook(toolbarLook());
        m_mainToolbar.setButtons(mainToolbarButtons());
        m_mainToolbar.layout(width() - 2 * kEdgeMargin);
    }
    const bool shown = m_settings.showToolbar && !m_mainToolbarHide.hidden();
    if (!shown)
        m_mainToolbar.setMenuOpen(false);
    const QImage bar = shown ? m_mainToolbar.texture() : QImage();
    if (const qint64 key = bar.isNull() ? 0 : bar.cacheKey(); key != m_mainToolbarTexture) {
        m_mainToolbarTexture = key;
        m_renderer.setOverlay(Renderer::ToolbarLayer, bar);
        requestUpdate();
    }
    const QImage menu = shown ? m_mainToolbar.menuTexture() : QImage();
    if (const qint64 key = menu.isNull() ? 0 : menu.cacheKey(); key != m_toolbarMenuTexture) {
        m_toolbarMenuTexture = key;
        m_renderer.setOverlay(Renderer::ToolbarMenuLayer, menu);
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

QRectF ViewerWindow::toolbarMenuRect() const
{
    const QRectF bar = mainToolbarRect();
    const QSizeF menu = m_mainToolbar.menuSize();
    if (bar.isEmpty() || menu.isEmpty())
        return {};
    // Below "…", its right edge under the button's, within the window's margins.
    const double dpr = devicePixelRatio();
    const QSizeF size((menu * dpr).toSize());
    const double right = bar.left() + m_mainToolbar.buttonRect(Toolbar::kMore).right() * dpr;
    const double left = std::clamp(right - size.width(), kEdgeMargin * dpr,
                                   std::max(kEdgeMargin * dpr, deviceSize().width() - kEdgeMargin * dpr - size.width()));
    return QRectF(QPointF(std::round(left), std::round(bar.bottom() + kMenuGap * dpr)), size);
}

double ViewerWindow::mainToolbarReserve() const
{
    // What it takes from the window's top while it shows: hidden by itself, its place goes to what is
    // below it, as when it is switched off (the owner, D-113).
    const QRectF bar = mainToolbarRect();
    return bar.isEmpty() ? 0.0 : bar.height() / devicePixelRatio() + kEdgeMargin;
}

int ViewerWindow::mainToolbarAt(const QPointF &position) const
{
    const QRectF rect = mainToolbarRect();
    if (rect.isEmpty())
        return -1;
    const double dpr = devicePixelRatio();
    return m_mainToolbar.buttonAt((position * dpr - rect.topLeft()) / dpr);
}

int ViewerWindow::toolbarMenuAt(const QPointF &position) const
{
    const QRectF rect = toolbarMenuRect();
    if (rect.isEmpty())
        return -1;
    const double dpr = devicePixelRatio();
    return m_mainToolbar.menuRowAt((position * dpr - rect.topLeft()) / dpr);
}

void ViewerWindow::setMainToolbarHover(int id)
{
    if (!m_mainToolbar.setHover(id))
        return;
    hideTip(TipSource::Toolbar);
    if (id != -1)
        m_tooltipTimer.start(kTooltipDelayMs);
    else if (m_slideshowBar.hover() == -1)
        m_tooltipTimer.stop();
    updateCursor();
    updateMainToolbar();
}

void ViewerWindow::setSlideshowBarHover(int id)
{
    if (!m_slideshowBar.setHover(id))
        return;
    hideTip(TipSource::Toolbar);
    if (id != -1)
        m_tooltipTimer.start(kTooltipDelayMs);
    else if (m_mainToolbar.hover() == -1)
        m_tooltipTimer.stop();
    updateCursor();
    updateSlideshowToolbar();
}

void ViewerWindow::showTip(TipSource source, const QString &text, const QRectF &anchor, TipPlace place)
{
    if (!m_rendererReady || text.isEmpty())
        return;
    const bool redraw = source != m_tipSource || text != m_tipText || m_tipSize.isEmpty() || m_tipDpr != devicePixelRatio();
    m_tipSource = source;
    m_tipAnchor = anchor;
    m_tipPlace = place;
    if (redraw) {
        // The panels' background with the hover's colour over it, as a hovered button or thumbnail.
        m_tipText = text;
        const QFont font = QGuiApplication::font();
        const QFontMetricsF metrics(font);
        const QStringList lines = text.split(QLatin1Char('\n'));
        double width = 0;
        for (const QString &line : lines)
            width = std::max(width, metrics.horizontalAdvance(line));
        const double padX = 8.0, padY = 4.0, line = metrics.height();
        const QSizeF logical(std::ceil(width + 2 * padX), std::ceil(lines.size() * line + 2 * padY));
        const qreal dpr = devicePixelRatio();
        QImage tip((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
        tip.setDevicePixelRatio(dpr);
        tip.fill(Qt::transparent);
        {
            QPainter painter(&tip);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            const QRectF box(QPointF(0, 0), logical);
            const double radius = std::min(kPanelRadius, logical.height() / 2);
            painter.setBrush(m_settings.panelBackground());
            painter.drawRoundedRect(box, radius, radius);
            painter.setBrush(m_settings.highlightTile(true));
            painter.drawRoundedRect(box, radius, radius);
            painter.setFont(font);
            painter.setPen(m_settings.panelText(Settings::kPanelValueGrey));
            for (qsizetype i = 0; i < lines.size(); ++i)
                painter.drawText(QRectF(padX, padY + i * line, logical.width() - 2 * padX, line),
                                 Qt::AlignLeft | Qt::AlignVCenter, lines.at(i));
        }
        m_tipSize = tip.size();
        m_tipDpr = dpr;
        m_renderer.setOverlay(Renderer::TooltipLayer, tip);
    }
    requestUpdate();
}

void ViewerWindow::hideTip(TipSource source)
{
    if (m_tipSource == TipSource::None || (source != TipSource::None && source != m_tipSource))
        return;
    m_tipSource = TipSource::None;
    m_tipText.clear();
    m_tipSize = QSize();
    if (m_rendererReady)
        m_renderer.setOverlay(Renderer::TooltipLayer, QImage());
    requestUpdate();
}

QRectF ViewerWindow::tipRect() const
{
    if (m_tipSource == TipSource::None || m_tipSize.isEmpty())
        return {};
    const double dpr = devicePixelRatio();
    const QSizeF size = QSizeF(m_tipSize) / dpr;
    QPointF at;
    switch (m_tipPlace) {
    case TipPlace::Below: at = QPointF(m_tipAnchor.center().x() - size.width() / 2, m_tipAnchor.bottom() + kTipGap); break;
    case TipPlace::Above: at = QPointF(m_tipAnchor.center().x() - size.width() / 2, m_tipAnchor.top() - kTipGap - size.height()); break;
    case TipPlace::Pointer: at = m_tipAnchor.topLeft() + QPointF(kTipGap, kPointerBody + kTipGap); break;
    }
    // Within the window's margins, as every component over the image (D-60).
    at.setX(std::clamp(at.x(), kEdgeMargin, std::max(kEdgeMargin, width() - kEdgeMargin - size.width())));
    at.setY(std::clamp(at.y(), kEdgeMargin, std::max(kEdgeMargin, height() - kEdgeMargin - size.height())));
    return QRectF(QPointF(std::round(at.x() * dpr), std::round(at.y() * dpr)), QSizeF(m_tipSize));
}

void ViewerWindow::showPendingTip()
{
    if (m_mainToolbar.hover() != -1 || m_slideshowBar.hover() != -1) {
        showToolbarTooltip();
        return;
    }
    const int slot = m_gallery.hover();
    const int file = slot >= 0 ? m_gallery.fileAt(slot) : -1;
    if (file < 0 || !galleryShown() || !m_settings.galleryTooltips || QApplication::activePopupWidget())
        return;
    showTip(TipSource::Gallery, galleryTipText(file), m_gallery.slotRect(slot), TipPlace::Above);
}

void ViewerWindow::showToolbarTooltip()
{
    // The hovered button's name and key, below it, on either bar.
    const bool main = m_mainToolbar.hover() != -1;
    const Toolbar &bar = main ? m_mainToolbar : m_slideshowBar;
    const int id = bar.hover();
    const Toolbar::Button *button = bar.button(id);
    const QRectF rect = main ? mainToolbarRect() : slideshowToolbarRect();
    if (!button || rect.isEmpty() || QApplication::activePopupWidget() || m_mainToolbar.menuOpen())
        return;
    //: The toolbar's "…" button, which holds the buttons a narrow window cannot show.
    QString text = id == Toolbar::kMore ? tr("More") : button->name;
    if (!button->shortcut.isEmpty())
        //: A toolbar button's tooltip: %1 its name, %2 its keyboard shortcut, e.g. "Gallery (G)".
        text = tr("%1 (%2)").arg(text, button->shortcut);
    // Below its button, kTipGap under the bar.
    const double dpr = devicePixelRatio();
    const QRectF square = bar.buttonRect(id);
    showTip(TipSource::Toolbar, text, QRectF(rect.left() / dpr + square.left(), rect.top() / dpr, square.width(), rect.height() / dpr),
            TipPlace::Below);
}

void ViewerWindow::toolbarButtonClicked(int id)
{
    // A button of either bar, or a row of the "…" panel, which closes first.
    hideTip();
    m_tooltipTimer.stop();
    if (id == Toolbar::kMore) {
        m_mainToolbar.setMenuOpen(!m_mainToolbar.menuOpen());
        updateMainToolbar();
        return;
    }
    closeToolbarMenu();
    if (id == kSlideshowPause) {
        run(Command::PlayPause);
    } else if (Command(id) == Command::OpenWith && mainToolbarRect().isValid()) { // its menu under the toolbar
        if (isCommandEnabled(Command::OpenWith)) {
            const QRectF bar = mainToolbarRect();
            const QRectF button = m_mainToolbar.buttonRect(id);
            const double dpr = devicePixelRatio();
            const double left = button.isEmpty() ? bar.right() / dpr - 4 : bar.left() / dpr + button.left();
            showOpenWithMenu(mapToGlobal(QPointF(left, bar.bottom() / dpr + 2).toPoint()));
        }
    } else if (id >= 0) {
        run(Command(id));
    }
}

void ViewerWindow::closeToolbarMenu()
{
    if (m_mainToolbar.setMenuOpen(false))
        updateMainToolbar();
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
    closeToolbarMenu();
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
    // One delay for all (the owner, D-113), each its own switch.
    const int delay = m_settings.autoHideMs;
    m_toolbarHide.configure(m_settings.slideshowToolbarAutoHide, delay);
    m_mainToolbarHide.configure(m_settings.toolbarAutoHide, delay);
    m_galleryHide.configure(m_settings.galleryAutoHide, delay);
    m_overlayHide.configure(m_settings.overlayAutoHide, delay);
    m_panelHide.configure(m_settings.panelAutoHide, delay);
}

void ViewerWindow::updateSlideshowToolbar()
{
    if (!m_rendererReady)
        return;
    const bool shown = m_slideshow.running() && !m_toolbarHide.hidden() && !m_slideshowInMainToolbar;
    if (shown) {
        m_slideshowBar.setLook(toolbarLook()); // the toolbar's look (the owner, D-113)
        m_slideshowBar.setButtons(slideshowBarButtons());
        m_slideshowBar.layout(width() - 2 * kEdgeMargin);
    }
    const QImage image = shown ? m_slideshowBar.texture() : QImage();
    const bool wasShown = m_slideshowBarShown;
    m_slideshowBarShown = shown && !image.isNull();
    if (const qint64 key = image.isNull() ? 0 : image.cacheKey(); key != m_slideshowBarTexture) {
        m_slideshowBarTexture = key;
        m_renderer.setOverlay(Renderer::SlideshowLayer, image);
        requestUpdate();
    }
    if (wasShown && !m_slideshowBarShown)
        setSlideshowBarHover(-1);
}

QRectF ViewerWindow::slideshowToolbarRect() const
{
    if (!m_slideshowBarShown)
        return {};
    // Centred below the toolbar and the top overlay where they show, else at the top margin (D-60).
    const double dpr = devicePixelRatio();
    double top = (kEdgeMargin + mainToolbarReserve()) * dpr;
    if (topOverlayVisible())
        top += m_topOverlaySize.height() + kEdgeMargin * dpr;
    const QSizeF size((m_slideshowBar.size() * dpr).toSize());
    return QRectF(QPointF(std::round((deviceSize().width() - size.width()) / 2.0), std::round(top)), size);
}

int ViewerWindow::slideshowBarAt(const QPointF &position) const
{
    const QRectF rect = slideshowToolbarRect();
    if (rect.isEmpty())
        return -1;
    const double dpr = devicePixelRatio();
    return m_slideshowBar.buttonAt((position * dpr - rect.topLeft()) / dpr);
}

void ViewerWindow::showSlideshowToolbar()
{
    if (!m_slideshow.running())
        return;
    m_toolbarHide.show();
    updateSlideshowToolbar();
}
