// ViewerWindow, part 4 (decision D-30): the command table that drives the keyboard and
// the context menu, and the dialogs. See viewer.h for the other parts.
#include "viewer.h"

#include "formats.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QStandardPaths>
#include <QtGui/private/qkeymapper_p.h> // the layout's alternatives for a key press, as QShortcut uses

#include <algorithm>

namespace {

constexpr double kZoomStep = 1.25;

} // namespace

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
    // Ctrl is Command on macOS. Single keys follow the viewer conventions; the shortcuts with
    // modifiers follow the platforms' (copy, quit, settings, trash).
    static const QList<CommandInfo> table = {
        {C::Open, {K(Qt::CTRL | Qt::Key_O)}, false},
        {C::ClearRecent, {}, false},
        {C::ShowInFolder, {K(Qt::CTRL | Qt::SHIFT | Qt::Key_E)}, false},
        {C::CopyImage, {K(Qt::CTRL | Qt::Key_C)}, false},
        {C::CopyPath, {K(Qt::CTRL | Qt::SHIFT | Qt::Key_C)}, false},
        {C::Rename, {K(Qt::Key_F2)}, false},
        {C::MoveToTrash, {K(Qt::Key_Delete), K(Qt::CTRL | Qt::Key_Backspace)}, false},
        {C::DeletePermanently, {K(Qt::SHIFT | Qt::Key_Delete)}, false},
        {C::UndoTrash, {K(Qt::CTRL | Qt::Key_Z)}, false},
        {C::Settings, {K(Qt::CTRL | Qt::Key_Comma)}, false},
        {C::Quit, {K(Qt::CTRL | Qt::Key_Q)}, false},
        {C::Previous, {K(Qt::Key_Left), K(Qt::Key_PageUp), K(Qt::Key_Backspace)}, true},
        {C::Next, {K(Qt::Key_Right), K(Qt::Key_PageDown), K(Qt::Key_Space)}, true},
        {C::First, {K(Qt::Key_Home)}, false},
        {C::Last, {K(Qt::Key_End)}, false},
        {C::ZoomIn, {K(Qt::Key_Plus), K(Qt::Key_Equal), K(Qt::CTRL | Qt::Key_Plus), K(Qt::CTRL | Qt::Key_Equal)}, true},
        {C::ZoomOut, {K(Qt::Key_Minus), K(Qt::CTRL | Qt::Key_Minus)}, true},
        {C::Fit, {K(Qt::Key_0), K(Qt::CTRL | Qt::Key_0)}, false},
        {C::ActualSize, {K(Qt::Key_1), K(Qt::CTRL | Qt::Key_1)}, false},
        {C::FullScreen, {K(Qt::Key_F), K(Qt::Key_F11)}, false},
        {C::Info, {K(Qt::Key_I)}, false},
        {C::InfoOverlay, {K(Qt::SHIFT | Qt::Key_I)}, false},
        {C::Checkerboard, {K(Qt::Key_B)}, false},
        {C::RotateClockwise, {K(Qt::Key_R)}, false},
        {C::RotateCounterclockwise, {K(Qt::SHIFT | Qt::Key_R)}, false},
        {C::FlipHorizontal, {K(Qt::Key_H)}, false},
        {C::FlipVertical, {K(Qt::Key_V)}, false},
        {C::ExposureUp, {K(Qt::Key_E)}, true},
        {C::ExposureDown, {K(Qt::SHIFT | Qt::Key_E)}, true},
        {C::ExposureReset, {K(Qt::CTRL | Qt::Key_E)}, false},
        {C::ToneMap, {K(Qt::Key_T)}, false},
        {C::ClipWarning, {K(Qt::Key_C)}, false},
        {C::PlayPause, {K(Qt::Key_K)}, false},
        {C::PreviousFrame, {K(Qt::Key_Comma)}, true},
        {C::NextFrame, {K(Qt::Key_Period)}, true},
        {C::Slideshow, {K(Qt::Key_S)}, false},
        {C::About, {}, false},
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
    //: "100 %" is a zoom percentage; write the percent sign as your language does.
    case Command::ActualSize: return tr("Actual Size (100 %)");
    case Command::FullScreen: return tr("Full Screen");
    case Command::Info: return tr("Information Panel");
    case Command::InfoOverlay: return tr("Information Overlay");
    case Command::Checkerboard: return tr("Checkerboard Background");
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
    case Command::PlayPause: return tr("Pause Animation");
    case Command::PreviousFrame: return tr("Previous Frame");
    case Command::NextFrame: return tr("Next Frame");
    case Command::Slideshow: return tr("Slideshow");
    case Command::About: return tr("About imageViewer");
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
    case Command::CopyPath: return !m_image.path.isEmpty();
    case Command::CopyImage: return hasImage && currentFileIsShown() && !m_copyBusy;
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
    case Command::PlayPause:
    case Command::PreviousFrame:
    case Command::NextFrame: return m_animation != nullptr;
    case Command::ZoomIn:
    case Command::ZoomOut:
    case Command::Fit:
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
    case Command::FullScreen: return visibility() == QWindow::FullScreen;
    case Command::Info: return m_showInfo;
    case Command::InfoOverlay: return topOverlayMode() != OverlayVisibility::Hidden;
    case Command::Checkerboard: return m_settings.checkerboard;
    case Command::ToneMap: return m_toneMap;
    case Command::ClipWarning: return m_clipWarning;
    case Command::PlayPause: return m_animationPaused;
    case Command::Slideshow: return m_slideshow;
    default: *checkable = false; return false;
    }
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
        startLoading(0);
        break;
    case Command::Last:
        m_direction = -1;
        startLoading(int(m_files.size()) - 1);
        break;
    case Command::ZoomIn: zoomAt(kZoomStep, centre); break;
    case Command::ZoomOut: zoomAt(1.0 / kZoomStep, centre); break;
    case Command::Fit: setFit(); break;
    case Command::ActualSize: setActualSize(); break;
    case Command::FullScreen: toggleFullScreen(); break;
    case Command::Info: toggleInfo(); break;
    case Command::InfoOverlay: toggleTopOverlay(); break;
    case Command::Checkerboard: toggleCheckerboard(); break;
    case Command::RotateClockwise: rotate(1); break;
    case Command::RotateCounterclockwise: rotate(-1); break;
    case Command::FlipHorizontal: flipHorizontal(); break;
    case Command::FlipVertical: flipVertical(); break;
    case Command::ExposureUp: adjustExposure(0.5f); break;
    case Command::ExposureDown: adjustExposure(-0.5f); break;
    case Command::ExposureReset: resetExposure(); break;
    case Command::ToneMap: toggleToneMap(); break;
    case Command::ClipWarning: toggleClipWarning(); break;
    case Command::PlayPause: togglePause(); break;
    case Command::PreviousFrame: stepFrame(-1); break;
    case Command::NextFrame: stepFrame(+1); break;
    case Command::Slideshow: toggleSlideshow(); break;
    case Command::About: showAbout(); break;
    case Command::AboutQt: QMessageBox::aboutQt(nullptr, tr("About Qt")); break;
    }
}

bool ViewerWindow::executeShortcut(QKeyEvent *e)
{
    // Every combination this press stands for on the current keyboard layout, as Qt's own
    // shortcuts see it (on AZERTY the "1" key types "&" and gives 1 with Shift).
    QList<QKeyCombination> candidates = QKeyMapper::possibleKeys(e);
    candidates.prepend(e->keyCombination());
    QList<QKeyCombination> pressed;
    for (const QKeyCombination candidate : std::as_const(candidates)) {
        Qt::KeyboardModifiers modifiers = candidate.keyboardModifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
        const Qt::Key key = candidate.key();
        // "+", "-" and the digits need Shift on many layouts: they match with or without it.
        if (key == Qt::Key_Plus || key == Qt::Key_Minus || key == Qt::Key_Equal || (key >= Qt::Key_0 && key <= Qt::Key_9))
            modifiers &= ~Qt::ShiftModifier;
        pressed << QKeyCombination(modifiers, key);
    }
    for (const CommandInfo &info : commands()) {
        for (const QKeySequence &shortcut : info.shortcuts) {
            if (shortcut.count() != 1 || !pressed.contains(shortcut[0]))
                continue;
            // Holding a toggle must not flip it back and forth, nor a held Delete trash a folder.
            if (!e->isAutoRepeat() || info.repeats) {
                if (isCommandEnabled(info.command))
                    execute(info.command);
            }
            return true;
        }
    }
    return false;
}

void ViewerWindow::addCommand(QMenu *menu, Command command)
{
    QAction *action = menu->addAction(commandText(command));
    const auto &table = commands();
    const auto it = std::find_if(table.cbegin(), table.cend(), [command](const CommandInfo &i) { return i.command == command; });
    if (it != table.cend() && !it->shortcuts.isEmpty()) {
        action->setShortcuts(it->shortcuts);
        action->setShortcutVisibleInContextMenu(true);
    }
    bool checkable = false;
    const bool checked = isCommandChecked(command, &checkable);
    action->setCheckable(checkable);
    action->setChecked(checked);
    action->setEnabled(isCommandEnabled(command));
    connect(action, &QAction::triggered, this, [this, command] { execute(command); });
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
        QString label = QFileInfo(file).fileName();
        label.replace(QLatin1Char('&'), QStringLiteral("&&")); // not a mnemonic
        QAction *action = recent->addAction(label);
        action->setToolTip(QDir::toNativeSeparators(file));
        connect(action, &QAction::triggered, this, [this, file] { openFile(file); });
    }
    if (!recent->isEmpty())
        recent->addSeparator();
    addCommand(recent, Command::ClearRecent);
    addCommand(&menu, Command::ShowInFolder);
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
    addCommand(view, Command::Fit);
    addCommand(view, Command::ActualSize);
    view->addSeparator();
    addCommand(view, Command::FullScreen);
    addCommand(view, Command::Info);
    addCommand(view, Command::InfoOverlay);
    addCommand(view, Command::Checkerboard);

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

    QMenu *go = menu.addMenu(tr("Go"));
    addCommand(go, Command::Previous);
    addCommand(go, Command::Next);
    go->addSeparator();
    addCommand(go, Command::First);
    addCommand(go, Command::Last);
    go->addSeparator();
    addCommand(go, Command::Slideshow);
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
    m_dragging = false;
    m_pressZone = Zone::None;
    menu.exec(globalPos);
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
    // Off, and back on to the mode it had (always, or on hover at the top), separately for a
    // window and for full screen; the menu shows it checked whenever it is on.
    const bool fullScreen = visibility() == QWindow::FullScreen;
    const OverlayVisibility mode = fullScreen ? m_settings.overlayFullScreen : m_settings.overlayWindow;
    OverlayVisibility &restore = m_overlayRestore[fullScreen ? 1 : 0];
    OverlayVisibility next = restore;
    if (mode != OverlayVisibility::Hidden) {
        restore = mode;
        next = OverlayVisibility::Hidden;
    }
    savePreference([fullScreen, next](Settings &s) { (fullScreen ? s.overlayFullScreen : s.overlayWindow) = next; });
    updateTopOverlay();
    requestUpdate();
}

void ViewerWindow::toggleCheckerboard()
{
    savePreference([on = !m_settings.checkerboard](Settings &s) { s.checkerboard = on; });
    requestUpdate();
}

void ViewerWindow::showSettings()
{
    SettingsDialog dialog(m_settings);
    makeTransient(dialog, this);
    // Apply and OK both deliver the values here; Cancel keeps whatever Apply already applied.
    connect(&dialog, &SettingsDialog::applied, this, &ViewerWindow::applySettings);
    dialog.exec();
}

void ViewerWindow::showAbout()
{
    QMessageBox box;
    box.setWindowTitle(tr("About imageViewer"));
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setText(QStringLiteral("<h3>imageViewer %1</h3><p>%2</p><p>© 2026 Cristallumnis, Lda.<br>%3</p><p>%4</p>"
                               "<p><a href=\"https://github.com/njardim/ImageViewer\">github.com/njardim/ImageViewer</a></p>")
                    .arg(QCoreApplication::applicationVersion().toHtmlEscaped(),
                         tr("Image viewer with verifiable SDR and HDR color fidelity."),
                         tr("Licensed under the Apache License, Version 2.0."),
                         tr("The licenses of the third-party components are in the <i>third-party</i> folder "
                            "installed with the application.")));
    box.addButton(tr("OK"), QMessageBox::AcceptRole);
    makeTransient(box, this);
    box.exec();
}
