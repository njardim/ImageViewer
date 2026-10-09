// ViewerWindow, part 2: which image is shown. The folder list and its order, loading from
// the preload cache or the decode thread, preloading the neighbours (decision D-33) and
// following changes on disk.
#include "viewer.h"

#include "folder.h"

#include <QCursor>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(lcNavigation, "imageviewer.navigation", QtWarningMsg)

namespace {
// Same directory, so the name decides; Windows and macOS file systems ignore case.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity kFileNameCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kFileNameCase = Qt::CaseSensitive;
#endif
} // namespace

void ViewerWindow::openFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        m_message = tr("File not found: %1").arg(path);
        editRecentFiles([&path](QStringList &recent) { recent.removeAll(path); });
        updateOverlay();
        return;
    }
    m_direction = 1;
    if (info.isDir()) {
        const QString folder = info.absoluteFilePath();
        listFolder(folder, [this, folder](QStringList files) {
            if (files.isEmpty()) { // keep the current list and image
                m_message = tr("The folder contains no supported images.");
                updateOverlay();
                return;
            }
            m_files = std::move(files);
            m_lastDirectory = folder;
            setFolder(folder);
            startLoading(0);
        });
        return;
    }
    // The image first; the rest of its folder follows from the background listing.
    m_files = {info.absoluteFilePath()};
    m_index = -1;
    m_lastDirectory = info.absolutePath();
    setFolder(m_lastDirectory);
    addRecentFile(info.absoluteFilePath());
    startLoading(0);
    relist();
}

QString ViewerWindow::currentPath() const
{
    return m_index >= 0 && m_index < m_files.size() ? m_files.at(m_index) : QString();
}

void ViewerWindow::startLoading(int index)
{
    if (index < 0 || index >= m_files.size())
        return;
    m_index = index;
    scheduleWork();
}

bool ViewerWindow::hasNeighbour(int delta) const
{
    if (m_files.size() < 2)
        return false;
    const int target = m_index + delta;
    return m_settings.loop || (target >= 0 && target < m_files.size());
}

void ViewerWindow::step(int delta)
{
    if (m_files.isEmpty())
        return;
    const int n = int(m_files.size());
    int target = m_index + delta;
    if (target < 0 || target >= n) {
        if (!m_settings.loop) {
            showNotice(delta > 0 ? tr("This is the last image.") : tr("This is the first image."));
            return;
        }
        target = (target % n + n) % n;
    }
    m_direction = delta > 0 ? 1 : -1;
    if (m_slideshow) // the next image comes a full interval after this one
        m_slideshowTimer.start();
    startLoading(target);
}

QStringList ViewerWindow::neighbourhood() const
{
    const QString current = currentPath();
    if (current.isEmpty())
        return {};
    QStringList paths = {current};
    const int n = int(m_files.size());
    for (const int delta : {m_direction, -m_direction}) {
        if (!hasNeighbour(delta))
            continue;
        const QString &path = m_files.at(((m_index + delta) % n + n) % n);
        if (!paths.contains(path)) // with two images both neighbours are the same file
            paths << path;
    }
    return paths;
}

bool ViewerWindow::needsDisplay(const QString &path, int limit) const
{
    return m_image.path != path || m_imageStale || m_shownLimit != limit;
}

void ViewerWindow::scheduleWork()
{
    // The cache holds the current image and its neighbours only (D-33).
    m_cache.retain(m_settings.preload ? neighbourhood() : QStringList());
    const QString current = currentPath();
    if (current.isEmpty())
        return;
    const int limit = textureLimit(current);
    if (needsDisplay(current, limit)) {
        if (std::optional<Image> cached = m_cache.find(current, limit)) {
            qCInfo(lcNavigation).noquote() << "shown from the cache:" << QFileInfo(current).fileName();
            showImage(std::move(*cached), limit);
        } else {
            m_loadingMessage = tr("Loading %1…").arg(displayFileName(QFileInfo(current).fileName()));
            m_message = m_loadingMessage;
            updateOverlay();
            // A running decode (perhaps of this very file) is waited for: decodeFinished() comes back here.
            if (!m_decodeBusy)
                startDecode(current, limit, 0);
            return;
        }
    } else if (!m_loadingMessage.isEmpty() && m_message == m_loadingMessage) {
        // Back to the image on screen before the other one arrived (Right, Left).
        m_message = m_image.error;
        m_loadingMessage.clear();
        updateOverlay();
    }
    // The requested image is on screen: use the idle decode thread for its neighbours.
    if (m_decodeBusy || !m_settings.preload || !m_rendererReady)
        return;
    const QStringList wanted = neighbourhood();
    for (qsizetype i = 1; i < wanted.size(); ++i) {
        const QString &path = wanted.at(i);
        const int neighbourLimit = textureLimit(path);
        if (m_cache.state(path, neighbourLimit) == ImageCache::State::Missing) {
            startDecode(path, neighbourLimit, m_cache.maxPreloadPixels());
            return;
        }
    }
}

void ViewerWindow::startDecode(const QString &path, int limit, qint64 maxPixels)
{
    m_decodeBusy = true;
    m_jobLimit = limit;
    m_jobGeneration = m_decodeGeneration;
    m_watcher.setFuture(QtConcurrent::run(&m_decodePool, [path, limit, maxPixels] {
        return decodeImage(path, limit, maxPixels);
    }));
}

void ViewerWindow::decodeFinished()
{
    // takeResult() moves the image out; result() would copy it and the future would keep
    // its own reference to the pixels alive until the next decode.
    Image image = m_watcher.future().takeResult();
    m_decodeBusy = false;
    if (m_jobGeneration != m_decodeGeneration) { // decoded for another language: decode again
        scheduleWork();
        return;
    }
    const int limit = m_jobLimit;
    m_cache.insert(image, limit); // kept only if it is still the current image or a neighbour
    const QString current = currentPath();
    if (image.path == current && limit == textureLimit(current) && !image.overPixelLimit
        && needsDisplay(current, limit)) {
        qCInfo(lcNavigation).noquote() << "decoded for display:" << QFileInfo(image.path).fileName();
        showImage(std::move(image), limit);
    } else if (image.isValid()) {
        const bool kept = m_cache.state(image.path, limit) == ImageCache::State::Ready;
        qCInfo(lcNavigation).noquote() << (kept ? "preloaded:" : "preloaded, but over the memory budget:")
                                       << QFileInfo(image.path).fileName();
    }
    scheduleWork();
}

void ViewerWindow::showImage(Image image, int limit)
{
    setTitle(QStringLiteral("%1 — imageViewer").arg(displayFileName(QFileInfo(image.path).fileName())));
    // The same file again (smaller texture, device loss, language, changed on disk) keeps
    // the view; a new file starts fitted.
    const bool sameFile = image.path == m_image.path;
    if (!image.isValid()) {
        m_message = image.error;
        image.pixels.reset();
        m_image = std::move(image);
        m_renderer.clearImage();
        stopAnimation();
    } else {
        m_message.clear();
        m_renderer.setImage(image.pixels, QSize(image.width, image.height));
        image.pixels.reset(); // the cache keeps them when preloading is on; the GPU has its copy
        m_image = std::move(image);
        if (!sameFile) {
            m_fit = true;
            m_pan = {};
            m_quarterTurns = 0;
            m_mirrored = false;
        }
        clampPan();
        startAnimation(); // or stops the previous one
    }
    m_shownLimit = limit;
    m_imageStale = false;
    m_loadingMessage.clear();
    // Editors often save by replacing the file: watch the path again each time it is shown.
    const QStringList watchedFiles = m_folderWatcher.files();
    if (!watchedFiles.isEmpty())
        m_folderWatcher.removePaths(watchedFiles);
    const QFileInfo shown(m_image.path);
    if (shown.exists()) {
        m_folderWatcher.addPath(m_image.path);
        // Changed while it was being decoded (before the watch above existed): look again.
        if (shown.size() != m_image.fileSize || shown.lastModified() != m_image.modified)
            m_folderTimer.start();
    }
    // At either end of a non-looping folder a side button may have nothing left to do.
    setHoverZone(zoneAt(mapFromGlobal(QCursor::pos(screen()))));
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::reloadCurrent()
{
    m_cache.remove(m_image.path);
    m_imageStale = true;
    scheduleWork();
}

void ViewerWindow::setFolder(const QString &folder)
{
    if (folder == m_folder)
        return;
    if (!m_folder.isEmpty())
        m_folderWatcher.removePath(m_folder);
    m_folder = folder;
    if (!m_folder.isEmpty())
        m_folderWatcher.addPath(m_folder);
}

void ViewerWindow::listFolder(const QString &folder, std::function<void(QStringList)> done)
{
    const quint64 generation = ++m_listing;
    QtConcurrent::run(&listImages, folder, m_settings.sortBy, m_settings.sortDescending)
        .then(this, [this, generation, done = std::move(done)](QStringList files) {
            if (generation == m_listing) // another folder or another order was asked for meanwhile
                done(std::move(files));
        });
}

void ViewerWindow::relist()
{
    if (m_folder.isEmpty())
        return;
    const QString folder = m_folder;
    listFolder(folder, [this, folder](QStringList files) {
        if (folder == m_folder)
            applyListing(std::move(files));
    });
}

void ViewerWindow::applyListing(QStringList files)
{
    const QString current = currentPath();
    const int previousIndex = m_index;
    if (!current.isEmpty() && !files.contains(current)) {
        // The name as it was opened may differ in case from the directory entry (Windows, macOS).
        const QString name = QFileInfo(current).fileName();
        const auto same = std::find_if(files.begin(), files.end(), [&name](const QString &f) {
            return QStringView(f).mid(f.lastIndexOf(QLatin1Char('/')) + 1).compare(name, kFileNameCase) == 0;
        });
        if (same != files.end())
            *same = current;
        // A file opened despite an unknown suffix stays in the list while it exists.
        else if (QFileInfo::exists(current) && QFileInfo(current).absolutePath() == m_folder)
            files.prepend(current);
    }
    m_files = std::move(files);
    const int index = current.isEmpty() ? -1 : int(m_files.indexOf(current));
    if (index >= 0) {
        m_index = index;
        scheduleWork(); // the neighbours may be different files now
    } else if (m_files.isEmpty()) {
        // Everything was deleted or moved away.
        m_index = -1;
        stopAnimation();
        m_image = Image();
        m_renderer.clearImage();
        m_cache.clear();
        setTitle(QStringLiteral("imageViewer"));
        m_message = tr("No images left in this folder.");
        setHoverZone(Zone::None);
        requestUpdate();
    } else {
        // The current file is gone: the next one takes its place, or the last one.
        m_index = -1;
        startLoading(std::clamp(previousIndex, 0, int(m_files.size()) - 1));
    }
    updateOverlay();
}

void ViewerWindow::refreshFolder()
{
    if (m_folder.isEmpty())
        return;
    if (!QFileInfo(m_folder).isDir()) { // the folder itself was removed or renamed
        m_files.clear();
        relist();
        return;
    }
    relist();
    // The shown image itself was rewritten (an editor saved it): show the new content.
    if (!m_image.path.isEmpty() && m_image.path == currentPath()) {
        const QFileInfo info(m_image.path);
        if (info.exists() && (info.size() != m_image.fileSize || info.lastModified() != m_image.modified)) {
            qCInfo(lcNavigation).noquote() << "changed on disk:" << info.fileName();
            reloadCurrent();
        } else if (info.exists() && !m_folderWatcher.files().contains(m_image.path)) {
            m_folderWatcher.addPath(m_image.path); // replaced by a new file of the same name
        }
    }
}

void ViewerWindow::removeCurrentFromList()
{
    const QString path = currentPath();
    m_cache.remove(path);
    editRecentFiles([&path](QStringList &recent) { recent.removeAll(path); });
    // The next image takes the removed one's place; after the last, the previous one.
    m_files.removeAt(m_index);
    stopAnimation();
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
    const int next = std::min(m_index, int(m_files.size()) - 1);
    m_index = -1;
    startLoading(next);
}

void ViewerWindow::addRecentFile(const QString &path)
{
    editRecentFiles([&path](QStringList &recent) {
        recent.removeAll(path);
        recent.prepend(path);
        while (recent.size() > kMaxRecentFiles)
            recent.removeLast();
    });
}

void ViewerWindow::editRecentFiles(const std::function<void(QStringList &)> &edit)
{
    m_recent = loadRecentFiles();
    edit(m_recent);
    saveRecentFiles(m_recent);
}
