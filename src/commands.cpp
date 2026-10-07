// ViewerWindow, part 4 (decision D-30): the command table that drives the keyboard and
// the context menu, file operations and dialogs. See viewer.h for the other parts.
#include "viewer.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLoggingCategory>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui/private/qkeymapper_p.h> // the layout's alternatives for a key press, as QShortcut uses

#include <algorithm>

Q_LOGGING_CATEGORY(lcFiles, "imageviewer.files", QtWarningMsg)

namespace {

constexpr double kZoomStep = 1.25;
constexpr int kMaxUndo = 50;

// Dialogs and menus are widgets; the viewer is a QWindow. Parenting their native window
// keeps them above it, centred on it and, on Wayland, positioned at all.
void makeTransient(QWidget &widget, QWindow *parent)
{
    widget.winId();
    if (QWindow *handle = widget.windowHandle())
        handle->setTransientParent(parent);
}

// Removes the trash's own record of a file that was taken back out of it, so the trash does
// not list a file that is no longer there (freedesktop info file, Windows $I file).
void forgetTrashRecord(const QString &inTrash)
{
    const QFileInfo item(inTrash);
#if defined(Q_OS_WIN)
    const QString name = item.fileName();
    if (name.startsWith(QLatin1String("$R")))
        QFile::remove(item.absolutePath() + QStringLiteral("/$I") + name.mid(2));
#elif defined(Q_OS_MACOS)
    Q_UNUSED(item); // the Finder keeps its "put back" data elsewhere and drops it by itself
#else
    // <trash>/files/<name> is described by <trash>/info/<name>.trashinfo.
    const QDir files = item.absoluteDir();
    if (files.dirName() == QLatin1String("files"))
        QFile::remove(QDir(files.absoluteFilePath(QStringLiteral("../info"))).absoluteFilePath(item.fileName() + QStringLiteral(".trashinfo")));
#endif
}

// Whether the trash's own record of `inTrash` still names `original`. Only the freedesktop
// trash keeps a readable record next to the file; elsewhere size and date decide.
bool trashRecordNames(const QString &inTrash, const QString &original)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Q_UNUSED(inTrash);
    Q_UNUSED(original);
    return true;
#else
    const QFileInfo item(inTrash);
    QFile record(QDir(item.absoluteDir().absoluteFilePath(QStringLiteral("../info"))).absoluteFilePath(item.fileName() + QStringLiteral(".trashinfo")));
    if (!record.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    while (!record.atEnd()) {
        const QByteArray line = record.readLine(64 * 1024).trimmed();
        if (line.startsWith("Path="))
            return QFile::decodeName(QByteArray::fromPercentEncoding(line.mid(5))) == original;
    }
    return false;
#endif
}

// Why `name` cannot be the new name of a file in `directory`, or an empty string.
QString renameProblem(const QString &name, const QString &directory, const QString &original)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QCoreApplication::translate("ViewerWindow", "Enter a name.");
    if (trimmed == QLatin1String(".") || trimmed == QLatin1String(".."))
        return QCoreApplication::translate("ViewerWindow", "This name is not allowed.");
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return QCoreApplication::translate("ViewerWindow", "A name cannot contain “/” or “\\”.");
    if (name.toUtf8().size() > 255)
        return QCoreApplication::translate("ViewerWindow", "The name is too long.");
#if defined(Q_OS_WIN)
    static const QString forbidden = QStringLiteral("<>:\"|?*");
    const bool reservedChar = std::any_of(name.cbegin(), name.cend(), [](QChar c) {
        return c.unicode() < 32 || forbidden.contains(c);
    });
    const QString stem = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    static const QStringList devices = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
                                        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
                                        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
                                        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
                                        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
                                        QStringLiteral("LPT8"), QStringLiteral("LPT9")};
    if (reservedChar || devices.contains(stem) || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        //: Windows forbids < > : " | ? *, control characters, device names such as CON, and a final dot or space.
        return QCoreApplication::translate("ViewerWindow", "Windows does not allow this name.");
#else
    if (name.contains(QChar(0)))
        return QCoreApplication::translate("ViewerWindow", "This name is not allowed.");
#endif
    const QFileInfo target(QDir(directory).absoluteFilePath(name));
    // A change of case alone is allowed: on case-insensitive file systems the "existing" file
    // is this one. Where both names do exist, renameFile() fails safely (rename never overwrites).
    if (target.exists() && name.compare(QFileInfo(original).fileName(), Qt::CaseInsensitive) != 0)
        return QCoreApplication::translate("ViewerWindow", "A file with this name already exists.");
    return {};
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
    case Command::CopyImage: return hasImage && currentFileIsShown() && !m_copyWatcher.isRunning();
    case Command::Rename:
    case Command::MoveToTrash:
    case Command::DeletePermanently: return currentFileIsShown(); // also a file that failed to decode
    case Command::UndoTrash: return !m_trashed.isEmpty();
    case Command::ClearRecent: return !m_recent.isEmpty();
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
    case Command::InfoOverlay: return topOverlayMode() != OverlayVisibility::Hidden;
    case Command::Checkerboard: return m_settings.checkerboard;
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
    case Command::ClearRecent:
        m_recent.clear();
        saveRecentFiles(m_recent);
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
    // Asynchronous: a file manager that is slow to start must not freeze the viewer.
    const QUrl folder = QUrl::fromLocalFile(QFileInfo(path).absolutePath());
    auto *dbus = new QProcess(this);
    connect(dbus, &QProcess::finished, this, [dbus, folder](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0)
            QDesktopServices::openUrl(folder);
        dbus->deleteLater();
    });
    connect(dbus, &QProcess::errorOccurred, this, [dbus, folder](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // the other errors end in finished()
        QDesktopServices::openUrl(folder);
        dbus->deleteLater();
    });
    dbus->start(QStringLiteral("dbus-send"),
                {QStringLiteral("--session"), QStringLiteral("--print-reply"), QStringLiteral("--reply-timeout=15000"),
                 QStringLiteral("--dest=org.freedesktop.FileManager1"), QStringLiteral("--type=method_call"),
                 QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                 QStringLiteral("array:string:") + url, QStringLiteral("string:")});
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
        box.setTextFormat(Qt::PlainText); // a file name is never markup
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
    if (!currentFileIsShown() || m_image.path != path) // the folder changed while the dialog was open
        return;
    QString inTrash;
    if (!QFile::moveToTrash(path, &inTrash)) {
#if defined(Q_OS_WIN)
        showNotice(tr("Cannot move “%1” to the Recycle Bin.").arg(name));
#else
        showNotice(tr("Cannot move “%1” to the trash.").arg(name));
#endif
        return;
    }
    qCInfo(lcFiles).noquote() << "moved to the trash:" << path << "->" << inTrash;
    if (!inTrash.isEmpty()) {
        const QFileInfo moved(inTrash);
        m_trashed.append({path, inTrash, moved.size(), moved.lastModified()});
        while (m_trashed.size() > kMaxUndo)
            m_trashed.removeFirst();
    }
#if defined(Q_OS_WIN)
    showNotice(tr("Moved “%1” to the Recycle Bin").arg(name));
#else
    showNotice(tr("Moved “%1” to the trash").arg(name));
#endif
    removeCurrentFromList();
}

void ViewerWindow::deletePermanently()
{
    if (!currentFileIsShown())
        return;
    const QString path = m_image.path;
    const QString name = QFileInfo(path).fileName();
    // Always asked, whatever the trash setting: this cannot be undone.
    QMessageBox box;
    box.setTextFormat(Qt::PlainText); // a file name is never markup
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("imageViewer"));
    box.setText(tr("Delete “%1” permanently?").arg(name));
    box.setInformativeText(tr("The file does not go to the trash and cannot be restored."));
    QPushButton *remove = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    makeTransient(box, this);
    box.exec();
    if (box.clickedButton() != remove || !currentFileIsShown() || m_image.path != path)
        return;
    if (!QFile::remove(path)) {
        showNotice(tr("Cannot delete “%1”.").arg(name));
        return;
    }
    showNotice(tr("Deleted “%1”").arg(name));
    removeCurrentFromList();
}

void ViewerWindow::undoTrash()
{
    if (m_trashed.isEmpty())
        return;
    const TrashedFile entry = m_trashed.takeLast();
    const QString name = QFileInfo(entry.original).fileName();
    // The trash may have been emptied, and another file trashed later under the same name:
    // only the file that was moved there goes back.
    const QFileInfo inTrash(entry.inTrash);
    if (!inTrash.exists() || inTrash.size() != entry.size || inTrash.lastModified() != entry.modified
        || !trashRecordNames(entry.inTrash, entry.original)) {
        showNotice(tr("“%1” is no longer in the trash.").arg(name));
        return;
    }
    if (QFileInfo::exists(entry.original)) {
        m_trashed.append(entry); // the user may rename or move the other file and try again
        showNotice(tr("Cannot restore “%1”: a file with that name exists.").arg(name));
        return;
    }
    // Same volume (each volume has its own trash), so this is a rename, not a copy.
    if (!QFile::rename(entry.inTrash, entry.original)) {
        showNotice(tr("Cannot restore “%1”.").arg(name));
        return;
    }
    forgetTrashRecord(entry.inTrash);
    qCInfo(lcFiles).noquote() << "restored from the trash:" << entry.inTrash << "->" << entry.original;
    showNotice(tr("Restored “%1”").arg(name));
    openFile(entry.original);
}

void ViewerWindow::renameFile()
{
    if (!currentFileIsShown())
        return;
    const QString path = m_image.path;
    const QFileInfo info(path);
    const QString directory = info.absolutePath();

    QDialog dialog;
    dialog.setWindowTitle(tr("Rename"));
    auto *edit = new QLineEdit(info.fileName());
    edit->setAccessibleName(tr("New name"));
    // The name without its extension is selected, ready to be typed over.
    const qsizetype dot = info.fileName().lastIndexOf(QLatin1Char('.'));
    edit->setSelection(0, int(dot > 0 ? dot : info.fileName().size()));
    auto *problem = new QLabel;
    problem->setForegroundRole(QPalette::PlaceholderText);
    auto *buttons = new QDialogButtonBox;
    QPushButton *ok = buttons->addButton(tr("Rename"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto validate = [&] {
        const QString reason = edit->text() == info.fileName() ? QString() : renameProblem(edit->text(), directory, path);
        problem->setText(reason);
        ok->setEnabled(reason.isEmpty());
    };
    connect(edit, &QLineEdit::textChanged, &dialog, validate);
    validate();
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("New name:")));
    layout->addWidget(edit);
    layout->addWidget(problem);
    layout->addWidget(buttons);
    dialog.resize(std::max(420, dialog.sizeHint().width()), dialog.sizeHint().height());
    makeTransient(dialog, this);
    if (dialog.exec() != QDialog::Accepted || !currentFileIsShown() || m_image.path != path
        || edit->text() == info.fileName())
        return;
    const QString name = edit->text();
    if (!renameProblem(name, directory, path).isEmpty()) // the folder may have changed meanwhile
        return;
    const QString target = QDir(directory).absoluteFilePath(name);
    bool renamed;
    if (QFileInfo::exists(target)) {
        // Only a change of case on a case-insensitive file system gets here: go through a
        // temporary name, which every file system accepts.
        const QString temporary = QDir(directory).absoluteFilePath(
            QStringLiteral(".imageviewer-rename-%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
        renamed = QFile::rename(path, temporary);
        if (renamed && !QFile::rename(temporary, target)) {
            QFile::rename(temporary, path);
            renamed = false;
        }
    } else {
        renamed = QFile::rename(path, target);
    }
    if (!renamed) {
        showNotice(tr("Cannot rename “%1”.").arg(info.fileName()));
        return;
    }
    // The decoded image stays on screen under its new name.
    m_cache.remove(path);
    if (m_textureCapPath == path)
        m_textureCapPath = target; // it still needs the reduced size it was shown at
    m_image.path = target;
    const qsizetype recentIndex = m_recent.indexOf(path);
    if (recentIndex >= 0) {
        m_recent[recentIndex] = target;
        saveRecentFiles(m_recent);
    }
    m_files[m_index] = target;
    setTitle(QStringLiteral("%1 — imageViewer").arg(QFileInfo(target).fileName()));
    showNotice(tr("Renamed to “%1”").arg(QFileInfo(target).fileName()));
    relist(); // its place in the sort order may have changed
    const QStringList watchedFiles = m_folderWatcher.files();
    if (!watchedFiles.isEmpty())
        m_folderWatcher.removePaths(watchedFiles);
    m_folderWatcher.addPath(target);
}

void ViewerWindow::toggleTopOverlay()
{
    // Off, and back on to the mode it had (always, or on hover at the top), separately for a
    // window and for full screen; the menu shows it checked whenever it is on.
    const bool fullScreen = visibility() == QWindow::FullScreen;
    OverlayVisibility &mode = fullScreen ? m_settings.overlayFullScreen : m_settings.overlayWindow;
    OverlayVisibility &restore = m_overlayRestore[fullScreen ? 1 : 0];
    if (mode != OverlayVisibility::Hidden) {
        restore = mode;
        mode = OverlayVisibility::Hidden;
    } else {
        mode = restore;
    }
    m_settings.save();
    updateTopOverlay();
    requestUpdate();
}

void ViewerWindow::toggleCheckerboard()
{
    m_settings.checkerboard = !m_settings.checkerboard;
    m_settings.save();
    requestUpdate();
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
