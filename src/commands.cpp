// ViewerWindow, part 2 (decision D-30): the command table that drives the keyboard and
// the context menu, file operations and dialogs. Display and input live in viewer.cpp.
#include "viewer.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace {

constexpr double kZoomStep = 1.25;

// Dialogs and menus are widgets; the viewer is a QWindow. Parenting their native window
// keeps them above it, centred on it and, on Wayland, positioned at all.
void makeTransient(QWidget &widget, QWindow *parent)
{
    widget.winId();
    if (QWindow *handle = widget.windowHandle())
        handle->setTransientParent(parent);
}

} // namespace

const QList<ViewerWindow::CommandInfo> &ViewerWindow::commands()
{
    using C = Command;
    using K = QKeySequence;
    // Ctrl is Command on macOS. Single keys follow the viewer conventions; the shortcuts with
    // modifiers follow the platforms' (copy, quit, settings, trash).
    static const QList<CommandInfo> table = {
        {C::Open, {K(Qt::CTRL | Qt::Key_O)}, false},
        {C::ShowInFolder, {K(Qt::CTRL | Qt::SHIFT | Qt::Key_E)}, false},
        {C::CopyImage, {K(Qt::CTRL | Qt::Key_C)}, false},
        {C::CopyPath, {K(Qt::CTRL | Qt::SHIFT | Qt::Key_C)}, false},
        {C::MoveToTrash, {K(Qt::Key_Delete), K(Qt::CTRL | Qt::Key_Backspace)}, false},
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
        {C::RotateClockwise, {K(Qt::Key_R)}, false},
        {C::RotateCounterclockwise, {K(Qt::SHIFT | Qt::Key_R)}, false},
        {C::FlipHorizontal, {K(Qt::Key_H)}, false},
        {C::FlipVertical, {K(Qt::Key_V)}, false},
        {C::ExposureUp, {K(Qt::Key_E)}, true},
        {C::ExposureDown, {K(Qt::SHIFT | Qt::Key_E)}, true},
        {C::ExposureReset, {K(Qt::CTRL | Qt::Key_E)}, false},
        {C::ToneMap, {K(Qt::Key_T)}, false},
        {C::ClipWarning, {K(Qt::Key_C)}, false},
        {C::About, {}, false},
        {C::AboutQt, {}, false},
    };
    return table;
}

QString ViewerWindow::commandText(Command command) const
{
    switch (command) {
    case Command::Open: return tr("Open…");
#if defined(Q_OS_WIN)
    case Command::ShowInFolder: return tr("Show in Explorer");
    case Command::MoveToTrash: return m_settings.confirmTrash ? tr("Move to Recycle Bin…") : tr("Move to Recycle Bin");
#elif defined(Q_OS_MACOS)
    case Command::ShowInFolder: return tr("Show in Finder");
    case Command::MoveToTrash: return m_settings.confirmTrash ? tr("Move to Trash…") : tr("Move to Trash");
#else
    case Command::ShowInFolder: return tr("Show in File Manager");
    case Command::MoveToTrash: return m_settings.confirmTrash ? tr("Move to Trash…") : tr("Move to Trash");
#endif
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
    case Command::ActualSize: return tr("Actual Size (100 %)");
    case Command::FullScreen: return tr("Full Screen");
    case Command::Info: return tr("Information Panel");
    case Command::RotateClockwise: return tr("Rotate Clockwise");
    case Command::RotateCounterclockwise: return tr("Rotate Counterclockwise");
    case Command::FlipHorizontal: return tr("Flip Horizontally");
    case Command::FlipVertical: return tr("Flip Vertically");
    case Command::ExposureUp: return tr("Increase Exposure (+½ EV)");
    case Command::ExposureDown: return tr("Decrease Exposure (−½ EV)");
    case Command::ExposureReset: return tr("Reset Exposure");
    case Command::ToneMap: return tr("Tone Mapping (BT.2390)");
    case Command::ClipWarning: return tr("Highlight Altered Pixels");
    case Command::About: return tr("About imageViewer");
    case Command::AboutQt: return tr("About Qt");
    }
    return {};
}

bool ViewerWindow::currentFileIsShown() const
{
    return m_index >= 0 && m_index < m_files.size() && !m_image.path.isEmpty() && m_files.at(m_index) == m_image.path
           && !m_watcher.isRunning();
}

bool ViewerWindow::isCommandEnabled(Command command) const
{
    const bool hasImage = m_image.width > 0;
    switch (command) {
    case Command::ShowInFolder:
    case Command::CopyPath: return !m_image.path.isEmpty();
    case Command::CopyImage: return hasImage && currentFileIsShown() && !m_copyWatcher.isRunning();
    case Command::MoveToTrash: return currentFileIsShown(); // also a file that failed to decode
    case Command::Previous: return hasNeighbour(-1);
    case Command::Next: return hasNeighbour(+1);
    case Command::First:
    case Command::Last: return m_files.size() > 1;
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
    case Command::ToneMap: return m_toneMap;
    case Command::ClipWarning: return m_clipWarning;
    default: *checkable = false; return false;
    }
}

void ViewerWindow::execute(Command command)
{
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0;
    switch (command) {
    case Command::Open: showOpenDialog(); break;
    case Command::ShowInFolder: showInFolder(); break;
    case Command::CopyImage: copyImage(); break;
    case Command::CopyPath: copyPath(); break;
    case Command::MoveToTrash: moveToTrash(); break;
    case Command::Settings: showSettings(); break;
    case Command::Quit: close(); break;
    case Command::Previous: step(-1); break;
    case Command::Next: step(+1); break;
    case Command::First: startLoading(0); break;
    case Command::Last: startLoading(int(m_files.size()) - 1); break;
    case Command::ZoomIn: zoomAt(kZoomStep, centre); break;
    case Command::ZoomOut: zoomAt(1.0 / kZoomStep, centre); break;
    case Command::Fit: setFit(); break;
    case Command::ActualSize: setActualSize(); break;
    case Command::FullScreen: toggleFullScreen(); break;
    case Command::Info: toggleInfo(); break;
    case Command::RotateClockwise: rotate(1); break;
    case Command::RotateCounterclockwise: rotate(-1); break;
    case Command::FlipHorizontal: flipHorizontal(); break;
    case Command::FlipVertical: flipVertical(); break;
    case Command::ExposureUp: adjustExposure(0.5f); break;
    case Command::ExposureDown: adjustExposure(-0.5f); break;
    case Command::ExposureReset: resetExposure(); break;
    case Command::ToneMap: toggleToneMap(); break;
    case Command::ClipWarning: toggleClipWarning(); break;
    case Command::About: showAbout(); break;
    case Command::AboutQt: QMessageBox::aboutQt(nullptr, tr("About Qt")); break;
    }
}

bool ViewerWindow::executeShortcut(QKeyEvent *e)
{
    Qt::KeyboardModifiers modifiers = e->modifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const int key = e->key();
    // "+" and "-" need Shift on many keyboard layouts: they match with or without it.
    if (key == Qt::Key_Plus || key == Qt::Key_Minus || key == Qt::Key_Equal)
        modifiers &= ~Qt::ShiftModifier;
    const QKeyCombination pressed(modifiers, Qt::Key(key));
    for (const CommandInfo &info : commands()) {
        for (const QKeySequence &shortcut : info.shortcuts) {
            if (shortcut.count() != 1 || shortcut[0] != pressed)
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
    addCommand(&menu, Command::ShowInFolder);
    menu.addSeparator();
    addCommand(&menu, Command::CopyImage);
    addCommand(&menu, Command::CopyPath);
    addCommand(&menu, Command::MoveToTrash);
    menu.addSeparator();

    QMenu *view = menu.addMenu(tr("View"));
    addCommand(view, Command::ZoomIn);
    addCommand(view, Command::ZoomOut);
    addCommand(view, Command::Fit);
    addCommand(view, Command::ActualSize);
    view->addSeparator();
    addCommand(view, Command::FullScreen);
    addCommand(view, Command::Info);

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

void ViewerWindow::showInFolder()
{
    const QString path = m_image.path;
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    // Arguments are passed as a list (no shell), so file names cannot inject commands.
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#else
    // The freedesktop FileManager1 interface selects the file; without it, open the folder.
    // dbus-send splits arrays at commas, so they are percent-encoded in the URL.
    QString url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    url.replace(QLatin1Char(','), QStringLiteral("%2C"));
    QProcess dbus;
    dbus.start(QStringLiteral("dbus-send"),
               {QStringLiteral("--session"), QStringLiteral("--print-reply"),
                QStringLiteral("--dest=org.freedesktop.FileManager1"), QStringLiteral("--type=method_call"),
                QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                QStringLiteral("array:string:") + url, QStringLiteral("string:")});
    if (!dbus.waitForFinished(3000) || dbus.exitStatus() != QProcess::NormalExit || dbus.exitCode() != 0)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void ViewerWindow::copyImage()
{
    if (m_copyWatcher.isRunning() || !currentFileIsShown())
        return;
    // The displayed pixels live on the GPU, possibly reduced: decode the file again at full
    // resolution, on the decode thread (after any decode already running).
    m_copyPath = m_image.path;
    showNotice(tr("Copying the image…"));
    const QString path = m_copyPath;
    m_copyWatcher.setFuture(QtConcurrent::run(&m_decodePool, [path] { return decodeForClipboard(path); }));
}

void ViewerWindow::imageCopied()
{
    QImage image = m_copyWatcher.future().takeResult();
    if (image.isNull()) {
        showNotice(tr("Cannot copy the image."));
        return;
    }
    // The bitmap for image editors, the file itself for file managers and messaging apps.
    auto *mime = new QMimeData;
    mime->setImageData(image);
    mime->setUrls({QUrl::fromLocalFile(m_copyPath)});
    QGuiApplication::clipboard()->setMimeData(mime);
    showNotice(tr("Image copied to the clipboard"));
}

void ViewerWindow::copyPath()
{
    if (m_image.path.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(m_image.path));
    showNotice(tr("File path copied to the clipboard"));
}

void ViewerWindow::moveToTrash()
{
    if (!currentFileIsShown())
        return;
    const QString path = m_image.path;
    const QString name = QFileInfo(path).fileName();
    if (m_settings.confirmTrash) {
        QMessageBox box;
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("imageViewer"));
#if defined(Q_OS_WIN)
        box.setText(tr("Move “%1” to the Recycle Bin?").arg(name));
        QPushButton *move = box.addButton(tr("Move to Recycle Bin"), QMessageBox::AcceptRole);
#else
        box.setText(tr("Move “%1” to the trash?").arg(name));
        QPushButton *move = box.addButton(tr("Move to Trash"), QMessageBox::AcceptRole);
#endif
        QPushButton *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(move);
        box.setEscapeButton(cancel);
        auto *dontAsk = new QCheckBox(tr("Do not ask again"));
        box.setCheckBox(dontAsk);
        makeTransient(box, this);
        box.exec();
        if (box.clickedButton() != move)
            return;
        if (dontAsk->isChecked()) {
            m_settings.confirmTrash = false;
            m_settings.save();
        }
    }
    if (!QFile::moveToTrash(path)) {
#if defined(Q_OS_WIN)
        showNotice(tr("Cannot move “%1” to the Recycle Bin.").arg(name));
#else
        showNotice(tr("Cannot move “%1” to the trash.").arg(name));
#endif
        return;
    }
#if defined(Q_OS_WIN)
    showNotice(tr("Moved “%1” to the Recycle Bin").arg(name));
#else
    showNotice(tr("Moved “%1” to the trash").arg(name));
#endif
    // The next image takes the deleted one's place; after the last, the previous one.
    m_files.removeAt(m_index);
    m_image = Image();
    m_renderer.clearImage();
    if (m_files.isEmpty()) {
        m_index = -1;
        setTitle(QStringLiteral("imageViewer"));
        m_message = tr("No images left in this folder.");
        setHoverZone(Zone::None);
        updateOverlay();
        requestUpdate();
        return;
    }
    startLoading(std::min(m_index, int(m_files.size()) - 1));
}

void ViewerWindow::showSettings()
{
    m_settings.showInfo = m_showInfo;
    SettingsDialog dialog(m_settings);
    makeTransient(dialog, this);
    if (dialog.exec() == QDialog::Accepted)
        applySettings(dialog.settings());
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
