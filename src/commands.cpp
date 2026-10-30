// ViewerWindow, part 4 (decision D-30): the command table that drives the keyboard and
// the context menu, and the dialogs. See viewer.h for the other parts.
#include "viewer.h"

#include "appearance.h"
#include "openwith.h"

#include "formats.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaEnum>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QTranslator>
#include <QtGui/private/qkeymapper_p.h> // the layout's alternatives for a key press, as QShortcut uses

#include <algorithm>
#include <utility>

void makeTransient(QWidget &widget, QWindow *parent)
{
    widget.winId();
    if (QWindow *handle = widget.windowHandle())
        handle->setTransientParent(parent);
}

const QList<ViewerWindow::CommandInfo> &ViewerWindow::commands()
{
    using C = Command;
    using K = QKeySequence;
    // The convention of D-48 (Ctrl is Command on macOS): single keys for viewing and navigating;
    // Shift + a key for that key's second command (its reverse or its alternative); Ctrl for the
    // platforms' application commands (files, clipboard, undo, settings, quit, zoom) and resets;
    // never Alt. A key that a MacBook reaches only with fn (Home, End, Page Up and Down, the
    // F keys, forward Delete) is never a command's only shortcut. The first shortcut is the one
    // menus show, so macOS lists its own first. By the owner's choice (D-78, D-79), Open is O, and
    // Shift+O, its second command, opens with another application; the top overlay, shown at
    // startup, is I, and the information panel P (D-81).
#ifdef Q_OS_MACOS
    constexpr bool mac = true;
#else
    constexpr bool mac = false;
#endif
    const auto either = [](bool first, const K &a, const K &b) { return first ? QList<K>{a, b} : QList<K>{b, a}; };
    static const QList<CommandInfo> table = {
        {C::Open, {K(Qt::Key_O)}, false},
        {C::ClearRecent, {}, false},
        {C::ShowInFolder, {K(Qt::CTRL | Qt::SHIFT | Qt::Key_E)}, false},
        {C::OpenWithOther, {K(Qt::SHIFT | Qt::Key_O)}, false},
        {C::CopyImage, {K(Qt::CTRL | Qt::Key_C)}, false},
        {C::CopyPath, {K(Qt::CTRL | Qt::SHIFT | Qt::Key_C)}, false},
        {C::Rename, either(mac, K(Qt::Key_Return), K(Qt::Key_F2)), false},
        {C::MoveToTrash, either(mac, K(Qt::CTRL | Qt::Key_Backspace), K(Qt::Key_Delete)), false},
        {C::DeletePermanently, either(mac, K(Qt::CTRL | Qt::SHIFT | Qt::Key_Backspace), K(Qt::SHIFT | Qt::Key_Delete)), false},
        {C::UndoTrash, {K(Qt::CTRL | Qt::Key_Z)}, false},
        {C::Settings, {K(Qt::CTRL | Qt::Key_Comma)}, false},
        {C::Quit, {K(Qt::Key_Q), K(Qt::CTRL | Qt::Key_Q)}, false}, // Ctrl/⌘+Q: the platforms' own
        {C::Previous, {K(Qt::Key_Left), K(Qt::Key_PageUp)}, true},
        {C::Next, {K(Qt::Key_Right), K(Qt::Key_PageDown)}, true},
        {C::First, {K(Qt::SHIFT | Qt::Key_Left), K(Qt::Key_Home)}, false},
        {C::Last, {K(Qt::SHIFT | Qt::Key_Right), K(Qt::Key_End)}, false},
        {C::ZoomIn, {K(Qt::Key_Plus), K(Qt::Key_Equal), K(Qt::CTRL | Qt::Key_Plus), K(Qt::CTRL | Qt::Key_Equal)}, true},
        {C::ZoomOut, {K(Qt::Key_Minus), K(Qt::CTRL | Qt::Key_Minus)}, true},
        {C::Fit, {K(Qt::Key_0), K(Qt::CTRL | Qt::Key_0)}, false},
        {C::FitWidth, {K(Qt::Key_W)}, false},
        {C::FitHeight, {K(Qt::Key_H)}, false},
        {C::Fill, {K(Qt::Key_X)}, false},
        {C::LockZoom, {K(Qt::Key_Z)}, false},
        {C::ActualSize, {K(Qt::Key_1), K(Qt::CTRL | Qt::Key_1)}, false},
        {C::FullScreen, {K(Qt::Key_F), K(Qt::Key_F11)}, false},
        {C::Minimize, {K(Qt::Key_M)}, false}, // keyboard only, in no menu (D-78)
        {C::Info, {K(Qt::Key_P)}, false},
        {C::InfoOverlay, {K(Qt::Key_I)}, false},
        {C::Checkerboard, {K(Qt::Key_B)}, false},
        {C::Gallery, {K(Qt::Key_G)}, false},
        {C::RotateClockwise, {K(Qt::Key_R)}, false},
        {C::RotateCounterclockwise, {K(Qt::SHIFT | Qt::Key_R)}, false},
        {C::FlipHorizontal, {K(Qt::SHIFT | Qt::Key_W)}, false},
        {C::FlipVertical, {K(Qt::SHIFT | Qt::Key_H)}, false},
        {C::ExposureUp, {K(Qt::Key_E)}, true},
        {C::ExposureDown, {K(Qt::SHIFT | Qt::Key_E)}, true},
        {C::ExposureReset, {K(Qt::CTRL | Qt::Key_E)}, false},
        {C::ToneMap, {K(Qt::Key_T)}, false},
        {C::ClipWarning, {K(Qt::Key_C)}, false},
        {C::PlayPause, {K(Qt::Key_Space)}, false},
        {C::PreviousFrame, {K(Qt::CTRL | Qt::Key_Left)}, true},
        {C::NextFrame, {K(Qt::CTRL | Qt::Key_Right)}, true},
        {C::Slideshow, {K(Qt::Key_S)}, false},
        {C::About, {K(Qt::Key_V)}, false},
        {C::AboutQt, {}, false},
    };
    return table;
}

QString ViewerWindow::commandText(Command command) const
{
    switch (command) {
    case Command::Open: return tr("Open…");
    //: Empties the Open Recent menu.
    case Command::ClearRecent: return tr("Clear Menu");
#if defined(Q_OS_WIN)
    case Command::ShowInFolder: return tr("Show in Explorer");
    case Command::MoveToTrash: return m_settings.confirmTrash ? tr("Move to Recycle Bin…") : tr("Move to Recycle Bin");
    case Command::UndoTrash: return tr("Undo Move to Recycle Bin");
#elif defined(Q_OS_MACOS)
    case Command::ShowInFolder: return tr("Show in Finder");
    case Command::MoveToTrash: return m_settings.confirmTrash ? tr("Move to Trash…") : tr("Move to Trash");
    case Command::UndoTrash: return tr("Undo Move to Trash");
#else
    case Command::ShowInFolder: return tr("Show in File Manager");
    case Command::MoveToTrash: return m_settings.confirmTrash ? tr("Move to Trash…") : tr("Move to Trash");
    case Command::UndoTrash: return tr("Undo Move to Trash");
#endif
    case Command::OpenWithOther: return tr("Other Application…");
    case Command::Rename: return tr("Rename…");
    case Command::DeletePermanently: return tr("Delete Permanently…");
    case Command::CopyImage: return tr("Copy Image");
    case Command::CopyPath: return tr("Copy File Path");
    case Command::Settings: return tr("Settings…");
    case Command::Quit: return tr("Quit");
    case Command::Previous: return tr("Previous Image");
    case Command::Next: return tr("Next Image");
    case Command::First: return tr("First Image");
    case Command::Last: return tr("Last Image");
    case Command::ZoomIn: return tr("Zoom In");
    case Command::ZoomOut: return tr("Zoom Out");
    case Command::Fit: return tr("Fit to Window");
    case Command::FitWidth: return tr("Fit to Width");
    case Command::FitHeight: return tr("Fit to Height");
    case Command::Fill: return tr("Fill Window");
    case Command::LockZoom: return tr("Lock Zoom");
    //: "100 %" is a zoom percentage; write the percent sign as your language does.
    case Command::ActualSize: return tr("Actual Size (100 %)");
    case Command::FullScreen: return tr("Full Screen");
    case Command::Minimize: return tr("Minimize");
    case Command::Info: return tr("Information Panel");
    case Command::InfoOverlay: return tr("Information Overlay");
    case Command::Checkerboard: return tr("Checkerboard Background");
    case Command::Gallery: return tr("Gallery");
    case Command::RotateClockwise: return tr("Rotate Clockwise");
    case Command::RotateCounterclockwise: return tr("Rotate Counterclockwise");
    case Command::FlipHorizontal: return tr("Flip Horizontally");
    case Command::FlipVertical: return tr("Flip Vertically");
    //: EV: exposure value, photographic stops; ½ EV is half a stop.
    case Command::ExposureUp: return tr("Increase Exposure (+½ EV)");
    //: EV: exposure value, photographic stops; ½ EV is half a stop.
    case Command::ExposureDown: return tr("Decrease Exposure (−½ EV)");
    case Command::ExposureReset: return tr("Reset Exposure");
    case Command::ToneMap: return tr("Tone Mapping (BT.2390)");
    case Command::ClipWarning: return tr("Highlight Altered Pixels");
    //: Space: pauses the running slideshow, otherwise the animation.
    case Command::PlayPause: return tr("Pause");
    case Command::PreviousFrame: return tr("Previous Frame");
    case Command::NextFrame: return tr("Next Frame");
    case Command::Slideshow: return tr("Slideshow");
    case Command::About: return tr("About ImageViewer");
    case Command::AboutQt: return tr("About Qt");
    }
    return {};
}

bool ViewerWindow::currentFileIsShown() const
{
    // Preloading keeps the decode thread busy in the background: that does not matter here.
    return !m_image.path.isEmpty() && currentPath() == m_image.path;
}

bool ViewerWindow::isCommandEnabled(Command command) const
{
    const bool hasImage = m_image.width > 0;
    switch (command) {
    case Command::ShowInFolder:
    case Command::OpenWithOther:
    case Command::CopyPath: return !m_image.path.isEmpty();
    case Command::CopyImage: return hasImage && currentFileIsShown(); // during a copy, the next one waits (copyImage)
    case Command::Rename:
    case Command::MoveToTrash:
    case Command::DeletePermanently: return currentFileIsShown(); // also a file that failed to decode
    case Command::UndoTrash: return !m_trashed.isEmpty();
    case Command::ClearRecent: return !m_recent.isEmpty();
    case Command::Previous: return hasNeighbour(-1);
    case Command::Next: return hasNeighbour(+1);
    case Command::First:
    case Command::Last: return m_files.size() > 1;
    case Command::Slideshow: return m_slideshow || m_files.size() > 1; // a running one can always be stopped
    case Command::PlayPause: return m_slideshow || m_animation != nullptr; // the slideshow first
    case Command::PreviousFrame:
    case Command::NextFrame: return m_animation != nullptr;
    case Command::ZoomIn:
    case Command::ZoomOut:
    case Command::Fit:
    case Command::FitWidth:
    case Command::FitHeight:
    case Command::Fill:
    case Command::ActualSize:
    case Command::RotateClockwise:
    case Command::RotateCounterclockwise:
    case Command::FlipHorizontal:
    case Command::FlipVertical:
    case Command::ExposureUp:
    case Command::ExposureDown:
    case Command::ExposureReset: return hasImage;
    default: return true;
    }
}

bool ViewerWindow::isCommandChecked(Command command, bool *checkable) const
{
    *checkable = true;
    switch (command) {
    case Command::Fit: return m_fit == FitMode::Window;
    case Command::FitWidth: return m_fit == FitMode::Width;
    case Command::FitHeight: return m_fit == FitMode::Height;
    case Command::Fill: return m_fit == FitMode::Fill;
    case Command::ActualSize: return m_fit == FitMode::Actual;
    case Command::LockZoom: return m_settings.lockZoom;
    case Command::FullScreen: return visibility() == QWindow::FullScreen;
    case Command::Info: return m_settings.showInfo;
    case Command::InfoOverlay: return topOverlayMode() != OverlayVisibility::Hidden;
    case Command::Checkerboard: return m_settings.checkerboard;
    case Command::Gallery: return m_settings.showGallery;
    case Command::ToneMap: return m_settings.toneMap;
    case Command::ClipWarning: return m_settings.clipWarning;
    case Command::PlayPause: return m_slideshow ? m_slideshowPaused : m_animationPaused;
    case Command::Slideshow: return m_slideshow;
    default: *checkable = false; return false;
    }
}

void ViewerWindow::run(Command command)
{
    // One way in for every command, however it is given (D-85): the same checks, the same work.
    if (isCommandEnabled(command))
        execute(command);
}

void ViewerWindow::execute(Command command)
{
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0;
    switch (command) {
    case Command::Open: showOpenDialog(); break;
    case Command::ClearRecent:
        editRecentFiles([](QStringList &recent) { recent.clear(); });
        break;
    case Command::ShowInFolder: showInFolder(); break;
    case Command::OpenWithOther: openWithOtherApplication(); break;
    case Command::CopyImage: copyImage(); break;
    case Command::CopyPath: copyPath(); break;
    case Command::Rename: renameFile(); break;
    case Command::MoveToTrash: moveToTrash(); break;
    case Command::DeletePermanently: deletePermanently(); break;
    case Command::UndoTrash: undoTrash(); break;
    case Command::Settings: showSettings(); break;
    case Command::Quit: close(); break;
    case Command::Previous: step(-1); break;
    case Command::Next: step(+1); break;
    case Command::First:
        m_direction = 1;
        restartSlideshowInterval();
        startLoading(0);
        break;
    case Command::Last:
        m_direction = -1;
        restartSlideshowInterval();
        startLoading(int(m_files.size()) - 1);
        break;
    case Command::ZoomIn: zoomAt(kZoomStep, centre); break;
    case Command::ZoomOut: zoomAt(1.0 / kZoomStep, centre); break;
    case Command::Fit: chooseFit(FitMode::Window); break;
    case Command::FitWidth: chooseFit(FitMode::Width); break;
    case Command::FitHeight: chooseFit(FitMode::Height); break;
    case Command::Fill: chooseFit(FitMode::Fill); break;
    case Command::LockZoom: toggleLockZoom(); break;
    case Command::ActualSize: chooseFit(FitMode::Actual); break;
    case Command::FullScreen: toggleFullScreen(); break;
    case Command::Minimize: minimize(); break;
    case Command::Info: toggleInfo(); break;
    case Command::InfoOverlay: toggleTopOverlay(); break;
    case Command::Checkerboard: toggleCheckerboard(); break;
    case Command::Gallery: toggleGallery(); break;
    case Command::RotateClockwise: rotate(1); break;
    case Command::RotateCounterclockwise: rotate(-1); break;
    case Command::FlipHorizontal: flipHorizontal(); break;
    case Command::FlipVertical: flipVertical(); break;
    case Command::ExposureUp: adjustExposure(0.5f); break;
    case Command::ExposureDown: adjustExposure(-0.5f); break;
    case Command::ExposureReset: resetExposure(); break;
    case Command::ToneMap: toggleToneMap(); break;
    case Command::ClipWarning: toggleClipWarning(); break;
    case Command::PlayPause: m_slideshow ? toggleSlideshowPause() : togglePause(); break;
    case Command::PreviousFrame: stepFrame(-1); break;
    case Command::NextFrame: stepFrame(+1); break;
    case Command::Slideshow: toggleSlideshow(); break;
    case Command::About: showAbout(); break;
    case Command::AboutQt: QMessageBox::aboutQt(nullptr, tr("About Qt")); break;
    }
}

QString ViewerWindow::commandKey(Command command)
{
    return QString::fromLatin1(QMetaEnum::fromType<Command>().valueToKey(int(command)));
}

QList<QKeySequence> ViewerWindow::shortcutsFor(Command command) const
{
    const auto user = m_settings.shortcuts.constFind(commandKey(command));
    if (user != m_settings.shortcuts.cend())
        return *user;
    const auto &table = commands();
    const auto it = std::find_if(table.cbegin(), table.cend(), [command](const CommandInfo &i) { return i.command == command; });
    return it != table.cend() ? it->shortcuts : QList<QKeySequence>();
}

bool ViewerWindow::executeShortcut(QKeyEvent *e)
{
    // Every combination this press stands for on the current keyboard layout, as Qt's own
    // shortcuts see it (on AZERTY the "1" key types "&" and gives 1 with Shift).
    // The key as pressed is tried first: Shift+1 on a US keyboard is "!" before it is "1".
    QList<QKeyCombination> candidates = QKeyMapper::possibleKeys(e);
    candidates.prepend(e->keyCombination());
    for (const QKeyCombination candidate : std::as_const(candidates)) {
        const QKeyCombination pressed = comparableKey(candidate);
        for (const CommandInfo &info : commands()) {
            for (const QKeySequence &shortcut : shortcutsFor(info.command)) {
                if (shortcut.count() != 1 || comparableKey(shortcut[0]) != pressed)
                    continue;
                // Holding a toggle must not flip it back and forth, nor a held Delete trash a folder.
                if (!e->isAutoRepeat() || info.repeats)
                    run(info.command);
                return true;
            }
        }
    }
    return false;
}

void ViewerWindow::addCommand(QMenu *menu, Command command)
{
    QString text = commandText(command);
#ifdef Q_OS_MACOS
    // A space after every label: keep it (the owner, D-97). Qt's macOS style draws a one-key shortcut
    // (V, B) in a field as wide as the font's widest glyph, ending at the menu's edge, while the menu
    // keeps for its shortcuts only the widest one's own width; in a menu of one-key shortcuts the
    // longest label then runs into its key ("About ImageViewerV"). Every label, since which one is
    // the longest depends on the language; in code, not in the translations.
    text += QLatin1Char(' ');
#endif
    QAction *action = menu->addAction(text);
    if (const QList<QKeySequence> shortcuts = shortcutsFor(command); !shortcuts.isEmpty()) {
        action->setShortcuts(shortcuts);
        action->setShortcutVisibleInContextMenu(true);
    }
    bool checkable = false;
    const bool checked = isCommandChecked(command, &checkable);
    action->setCheckable(checkable);
    action->setChecked(checked);
    action->setEnabled(isCommandEnabled(command));
    connect(action, &QAction::triggered, this, [this, command] { m_menuChoice = [this, command] { run(command); }; });
}

#ifdef Q_OS_MACOS
namespace {

// Qt names the application menu's About and Settings items itself, in English ("About %1",
// "Preferences..."), through QCoreApplication::translate() in the contexts "QMenuBar" and
// "MAC_APPLICATION_MENU" (qcocoamenuitem.mm): this translator answers with our own translations of
// those two commands, the context menu's, in the language shown (the owner, D-96). Installed after
// the application's catalogs, so asked before them.
class ApplicationMenuTranslator : public QTranslator
{
public:
    QString about;    // "About %1": the application's name, which Qt puts in, as %1
    QString settings; // "Settings…"

    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override
    {
        if (qstrcmp(context, "MAC_APPLICATION_MENU") != 0 && qstrcmp(context, "QMenuBar") != 0)
            return {};
        if (qstrcmp(source, "About %1") == 0)
            return about;
        if (qstrcmp(source, "Preferences...") == 0)
            return settings;
        return {};
    }
};

} // namespace
#endif

void ViewerWindow::installApplicationMenu()
{
#ifdef Q_OS_MACOS
    // About ImageViewer and Settings… (⌘,) in the application menu, where Mac users look for them,
    // each with a separator after it, the menu's own (the owner, D-96): the application's menu bar
    // (no parent: every window's), whose one menu Qt moves the actions out of by their roles, and
    // hides once empty. Nothing else changes: the menu bar is no window, so closing the viewer's
    // window still quits. Through run(), once the menu is gone (D-85).
    m_menuBar = std::make_unique<QMenuBar>();
    QMenu *menu = m_menuBar->addMenu(QString());
    m_aboutAction = menu->addAction(QString());
    m_aboutAction->setMenuRole(QAction::AboutRole);
    connect(m_aboutAction, &QAction::triggered, this, [this] { run(Command::About); }, Qt::QueuedConnection);
    m_settingsAction = menu->addAction(QString());
    m_settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(m_settingsAction, &QAction::triggered, this, [this] { run(Command::Settings); }, Qt::QueuedConnection);
    m_menuTranslator = std::make_unique<ApplicationMenuTranslator>();
    translateApplicationMenu();
#endif
}

void ViewerWindow::translateApplicationMenu()
{
#ifdef Q_OS_MACOS
    if (!m_menuTranslator)
        return;
    auto *translator = static_cast<ApplicationMenuTranslator *>(m_menuTranslator.get());
    translator->about = commandText(Command::About).replace(QStringLiteral("ImageViewer"), QStringLiteral("%1"));
    translator->settings = commandText(Command::Settings);
    QCoreApplication::removeTranslator(translator);
    QCoreApplication::installTranslator(translator); // after applyLanguage()'s catalogs: asked first
    // A new text makes Qt name the items again, through the translator.
    m_aboutAction->setText(commandText(Command::About));
    m_settingsAction->setText(commandText(Command::Settings));
#endif
}

void ViewerWindow::showContextMenu(const QPoint &globalPos)
{
    // Grouped like the menus of desktop applications (decision D-30): frequent file actions
    // at the top level, view, image and colour controls in submenus, application last.
    QMenu menu;
    addCommand(&menu, Command::Open);
    m_recent = loadRecentFiles(); // with the files other instances opened meanwhile
    QMenu *recent = menu.addMenu(tr("Open Recent"));
    recent->setToolTipsVisible(true);
    // Not checked for existence here (a network path can take seconds): openFile() reports
    // a missing file and drops it from the list.
    for (const QString &file : std::as_const(m_recent)) {
        QString label = displayFileName(QFileInfo(file).fileName());
        label.replace(QLatin1Char('&'), QStringLiteral("&&")); // not a mnemonic
        QAction *action = recent->addAction(label);
        // A file name is never markup: Qt would render "<font size=7>…" in a plain tooltip.
        action->setToolTip(QStringLiteral("<p style='white-space:pre'>%1</p>")
                               .arg(displayFileName(QDir::toNativeSeparators(file)).toHtmlEscaped()));
        connect(action, &QAction::triggered, this, [this, file] { m_menuChoice = [this, file] { openFile(file); }; });
    }
    if (!recent->isEmpty())
        recent->addSeparator();
    addCommand(recent, Command::ClearRecent);
    addCommand(&menu, Command::ShowInFolder);
    // The applications are looked up when the submenu opens (a scan of the system's registry).
    QMenu *openWithMenu = menu.addMenu(tr("Open With"));
    openWithMenu->setEnabled(isCommandEnabled(Command::OpenWithOther));
    connect(openWithMenu, &QMenu::aboutToShow, this, [this, openWithMenu] {
        if (!openWithMenu->isEmpty())
            return;
        const QString file = m_image.path;
        const QList<OpenWithApp> apps = openWithApps(file);
        for (const OpenWithApp &app : apps) {
            QString label = app.name;
            label.replace(QLatin1Char('&'), QStringLiteral("&&")); // not a mnemonic
            connect(openWithMenu->addAction(label), &QAction::triggered, this, [this, app, file] {
                m_menuChoice = [this, app, file] {
                    if (!openWith(app, file))
                        showNotice(tr("Cannot start “%1”.").arg(app.name));
                };
            });
        }
        if (apps.isEmpty())
            openWithMenu->addAction(tr("No applications found"))->setEnabled(false);
        openWithMenu->addSeparator();
        addCommand(openWithMenu, Command::OpenWithOther);
    });
    menu.addSeparator();
    addCommand(&menu, Command::CopyImage);
    addCommand(&menu, Command::CopyPath);
    menu.addSeparator();
    addCommand(&menu, Command::Rename);
    addCommand(&menu, Command::MoveToTrash);
    addCommand(&menu, Command::DeletePermanently);
    addCommand(&menu, Command::UndoTrash);
    menu.addSeparator();

    QMenu *view = menu.addMenu(tr("View"));
    addCommand(view, Command::ZoomIn);
    addCommand(view, Command::ZoomOut);
    addCommand(view, Command::LockZoom);
    view->addSeparator();
    addCommand(view, Command::Fit);
    addCommand(view, Command::FitWidth);
    addCommand(view, Command::FitHeight);
    addCommand(view, Command::Fill);
    addCommand(view, Command::ActualSize);
    view->addSeparator();
    addCommand(view, Command::FullScreen);
    addCommand(view, Command::Info);
    addCommand(view, Command::InfoOverlay);
    addCommand(view, Command::Checkerboard);
    addCommand(view, Command::Gallery);
    view->addSeparator();
    addCommand(view, Command::Slideshow);

    QMenu *image = menu.addMenu(tr("Image"));
    addCommand(image, Command::RotateClockwise);
    addCommand(image, Command::RotateCounterclockwise);
    image->addSeparator();
    addCommand(image, Command::FlipHorizontal);
    addCommand(image, Command::FlipVertical);

    //: "&&" is shown as a single "&".
    QMenu *color = menu.addMenu(tr("Color && HDR"));
    addCommand(color, Command::ExposureUp);
    addCommand(color, Command::ExposureDown);
    addCommand(color, Command::ExposureReset);
    color->addSeparator();
    addCommand(color, Command::ToneMap);
    addCommand(color, Command::ClipWarning);

    QMenu *go = menu.addMenu(tr("Jump to"));
    addCommand(go, Command::First);
    addCommand(go, Command::Last);
    go->addSeparator();
    addCommand(go, Command::Previous);
    addCommand(go, Command::Next);
    go->addSeparator();
    addCommand(go, Command::PlayPause);
    addCommand(go, Command::PreviousFrame);
    addCommand(go, Command::NextFrame);

    menu.addSeparator();
    addCommand(&menu, Command::Settings);
    QMenu *help = menu.addMenu(tr("Help"));
    addCommand(help, Command::About);
    addCommand(help, Command::AboutQt);
    menu.addSeparator();
    addCommand(&menu, Command::Quit);

    makeTransient(menu, this);
    cancelPress(); // the menu takes the release of a left press held meanwhile
    m_menuChoice = nullptr;
    menu.exec(globalPos);
    // What was chosen runs once the menu is gone, from the event loop, as a key's command does
    // (D-85): run from inside the menu's own events, a dialog lost the focus on macOS.
    if (std::function<void()> choice = std::exchange(m_menuChoice, nullptr))
        QMetaObject::invokeMethod(this, std::move(choice), Qt::QueuedConnection);
}

void ViewerWindow::showOpenDialog()
{
    QStringList patterns;
    for (const QString &suffix : supportedSuffixes())
        patterns << QStringLiteral("*.") + suffix;
    //: File dialog filters: keep "%1", ";;" and "(*)" exactly.
    const QString filter = tr("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
    QString start = !m_image.path.isEmpty() ? QFileInfo(m_image.path).absolutePath() : m_lastDirectory;
    if (start.isEmpty() || !QFileInfo(start).isDir())
        start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QFileDialog dialog(nullptr, tr("Open Image"), start, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    makeTransient(dialog, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
        openFile(dialog.selectedFiles().constFirst());
}

void ViewerWindow::toggleTopOverlay()
{
    // Like P for the panel, a switch (Settings::showOverlay); switched on where its mode is
    // "never", it shows there always.
    const bool fullScreen = visibility() == QWindow::FullScreen;
    const bool show = topOverlayMode() == OverlayVisibility::Hidden;
    savePreference([fullScreen, show](Settings &s) {
        s.showOverlay = show;
        OverlayVisibility &mode = fullScreen ? s.overlayFullScreen : s.overlayWindow;
        if (show && mode == OverlayVisibility::Hidden)
            mode = OverlayVisibility::Always;
    });
    if (topOverlayMode() == OverlayVisibility::Hover) { // shown now, then hidden as on hover
        setPointerAtTop(true);
        setPointerAtTop(false);
    }
    updateTopOverlay();
    requestUpdate();
}

void ViewerWindow::toggleCheckerboard()
{
    savePreference([on = !m_settings.checkerboard](Settings &s) { s.checkerboard = on; });
    requestUpdate();
}

void ViewerWindow::toggleLockZoom()
{
    const bool on = !m_settings.lockZoom;
    savePreference([on](Settings &s) { s.lockZoom = on; });
    showNotice(on ? tr("Zoom locked: the next images keep it") : tr("Zoom unlocked"));
}

void ViewerWindow::showSettings()
{
    SettingsDialog dialog(m_settings, [this] {
        QList<ShortcutCommand> list;
        for (const CommandInfo &info : commands()) {
            QString name = commandText(info.command);
            // Outside their submenu, "Clear Menu" and "Other Application…" need its title.
            if (info.command == Command::ClearRecent)
                name = tr("Open Recent") + QStringLiteral(" › ") + name;
            else if (info.command == Command::OpenWithOther)
                name = tr("Open With") + QStringLiteral(" › ") + name;
            list.append({commandKey(info.command), name, info.shortcuts});
        }
        return list;
    });
    makeTransient(dialog, this);
    // Apply and OK both deliver the values here; Cancel keeps whatever Apply already applied.
    connect(&dialog, &SettingsDialog::applied, this, &ViewerWindow::applySettings);
    dialog.exec();
}

void ViewerWindow::showAbout()
{
    QDialog dialog;
    dialog.setWindowTitle(tr("About ImageViewer"));
    auto *layout = new QGridLayout(&dialog);
    // The application's icon (D-66), 192 × 192 logical pixels with 8 around it (the owner,
    // 2026-10-27), sharp at any device pixel ratio.
    constexpr int kLogo = 192, kLogoMargin = 8;
    auto *logo = new QLabel;
    logo->setContentsMargins(kLogoMargin, kLogoMargin, kLogoMargin, kLogoMargin);
    logo->setFixedSize(kLogo + 2 * kLogoMargin, kLogo + 2 * kLogoMargin);
    const qreal dpr = devicePixelRatio();
    QPixmap icon(QStringLiteral(":/imageviewer.png"));
    icon = icon.scaled(QSize(kLogo, kLogo) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    icon.setDevicePixelRatio(dpr);
    logo->setPixmap(icon);
    layout->addWidget(logo, 0, 0, Qt::AlignVCenter);
    auto *text = new QLabel(
        QStringLiteral("<h2>ImageViewer %1</h2><p><b>%2</b></p><p>%3</p><p>%4</p>"
                       "<p>© 2026 Cristallumnis, Lda.<br>%5</p><p>%6</p>"
                       "<p><a href=\"https://github.com/njardim/ImageViewer\">github.com/njardim/ImageViewer</a></p>")
            .arg(QCoreApplication::applicationVersion().toHtmlEscaped(),
                 // The owner's text (D-78).
                 tr("An image viewer you can trust. See every image exactly as its file defines it, pixel by pixel, "
                    "and bit by bit, in SDR and HDR, on Windows, macOS and Linux."),
                 tr("It opens everything from everyday JPEGs to JPEG XL, AVIF, TIFF, OpenEXR, DPX and camera RAW, "
                    "with animations played at their own pace, and even the rarest formats are securely handled "
                    "in isolation, so a damaged file can’t take the viewer down."),
                 tr("When a highlight is brighter than your display can show, ImageViewer tells you how it handled "
                    "it instead of quietly deciding for you. Every build is checked against what the screen actually "
                    "receives, so what you see is exactly what the file says."),
                 tr("Licensed under the Apache License, Version 2.0."),
                 tr("The licenses of the third-party components are in the <i>third-party</i> folder "
                    "installed with the application.")));
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text->setOpenExternalLinks(true);
    text->setWordWrap(true);
    text->setMinimumWidth(qRound(500 * fontScale())); // as wide for its text at every font size (D-89)
    layout->addWidget(text, 0, 1, Qt::AlignTop);
    auto *buttons = new QDialogButtonBox;
    connect(buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole), &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(buttons, 1, 0, 1, 2);
    makeTransient(dialog, this);
    dialog.exec();
}
