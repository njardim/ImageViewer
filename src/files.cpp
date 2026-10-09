// ViewerWindow, part 5 (decisions D-28, D-41): file operations — show in the file manager,
// copy, rename, move to the trash and back, delete. See viewer.h for the other parts.
#include "viewer.h"

#include "openwith.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

Q_LOGGING_CATEGORY(lcFiles, "imageviewer.files", QtWarningMsg)

namespace {

constexpr int kMaxUndo = 50;

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
    const QString trash = QFileInfo(item.absolutePath()).absolutePath(); // <trash>/files/<name>
    QFile record(trash + QStringLiteral("/info/") + item.fileName() + QStringLiteral(".trashinfo"));
    if (!record.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    while (!record.atEnd()) {
        const QByteArray line = record.readLine(64 * 1024).trimmed();
        if (!line.startsWith("Path="))
            continue;
        const QString path = QFile::decodeName(QByteArray::fromPercentEncoding(line.mid(5)));
        if (QDir::isAbsolutePath(path))
            return path == original;
        // The trash of another volume, $topdir/.Trash-$uid or $topdir/.Trash/$uid, records the
        // path relative to $topdir.
        QString top = QFileInfo(trash).absolutePath();
        if (QFileInfo(top).fileName() == QLatin1String(".Trash"))
            top = QFileInfo(top).absolutePath();
        return QDir::cleanPath(top + QLatin1Char('/') + path) == original;
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

void ViewerWindow::openWithOtherApplication()
{
    const QString path = m_image.path;
    if (!path.isEmpty() && QFileInfo::exists(path))
        openWithOther(this, path); // cancelled or started: nothing to say
}

void ViewerWindow::copyImage()
{
    if (m_copyBusy || !currentFileIsShown())
        return;
    // The displayed pixels live on the GPU, possibly reduced: decode the file again at full
    // resolution, on the decode thread (after any decode already running).
    m_copyPath = m_image.path;
    showNotice(tr("Copying the image…"));
    const QString path = m_copyPath;
    m_copyBusy = true;
    m_copyWatcher.setFuture(QtConcurrent::run(&m_decodePool, [path] { return decodeForClipboard(path); }));
}

void ViewerWindow::imageCopied()
{
    QImage image = m_copyWatcher.future().takeResult();
    m_copyBusy = false;
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
    const QString name = displayFileName(QFileInfo(path).fileName());
    if (m_settings.confirmTrash) {
        QMessageBox box;
        box.setTextFormat(Qt::PlainText); // a file name is never markup
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("ImageViewer"));
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
            savePreference([](Settings &s) { s.confirmTrash = false; });
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
    const QString name = displayFileName(QFileInfo(path).fileName());
    // Always asked, whatever the trash setting: this cannot be undone.
    QMessageBox box;
    box.setTextFormat(Qt::PlainText); // a file name is never markup
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("ImageViewer"));
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
    const QString name = displayFileName(QFileInfo(entry.original).fileName());
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
    // Taken before the rename: the cache checks the file under its old name.
    std::optional<Image> kept = m_cache.find(path, m_shownLimit);
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
        showNotice(tr("Cannot rename “%1”.").arg(displayFileName(info.fileName())));
        return;
    }
    // The decoded image stays on screen and in the cache, under its new name.
    m_cache.remove(path);
    if (m_textureCapPath == path)
        m_textureCapPath = target; // it still needs the reduced size it was shown at
    m_image.path = target;
    editRecentFiles([&path, &target](QStringList &recent) {
        if (const qsizetype i = recent.indexOf(path); i >= 0)
            recent[i] = target;
    });
    m_files[m_index] = target;
    const QString shownName = displayFileName(QFileInfo(target).fileName());
    updateTitle();
    showNotice(tr("Renamed to “%1”").arg(shownName));
    relist(); // its place in the sort order may have changed
    if (kept) {
        kept->path = target; // renaming keeps the size and the modification time
        m_cache.insert(*kept, m_shownLimit);
    }
    const QStringList watchedFiles = m_folderWatcher.files();
    if (!watchedFiles.isEmpty())
        m_folderWatcher.removePaths(watchedFiles);
    m_folderWatcher.addPath(target);
}
