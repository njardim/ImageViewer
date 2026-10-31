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
#include <cmath>
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
        editRecentFiles([&path](QStringList &recent) { recent.removeAll(path); });
        reportOpenFailure(tr("File not found: %1").arg(displayFileName(path)));
        return;
    }
    m_direction = 1;
    if (info.isDir()) {
        const QString folder = info.absoluteFilePath();
        listFolder(folder, [this, folder](FolderListing listing) {
            if (listing.files.isEmpty()) { // keep the current list and image
                reportOpenFailure(tr("The folder contains no supported images."));
                return;
            }
            m_files = std::move(listing.files);
            forgetChangedThumbnails(listing.stamps); // the previous folder's
            m_galleryCentredOn = -2; // another folder: the gallery centres the image again
            m_lastDirectory = folder;
            setFolder(folder);
            startLoading(0);
        });
        return;
    }
    // The image first; the rest of its folder follows from the background listing.
    m_files = {info.absoluteFilePath()};
    m_galleryCentredOn = -2;
    m_index = -1;
    m_lastDirectory = info.absolutePath();
    setFolder(m_lastDirectory);
    addRecentFile(info.absoluteFilePath());
    startLoading(0);
    relist();
}

void ViewerWindow::reportOpenFailure(const QString &why)
{
    // The image on screen stays, and so does the window's middle: an error there would cover it
    // until the next image (D-63).
    if (m_image.width > 0) {
        showNotice(why);
        return;
    }
    m_error = why;
    updateOverlay();
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
    // Every way of stepping (keys, menus, mouse buttons, side zones, slideshow) is offered only
    // where there is a neighbour, as Previous and Next are enabled.
    if (!hasNeighbour(delta))
        return;
    const int n = int(m_files.size());
    const int target = ((m_index + delta) % n + n) % n;
    m_direction = delta > 0 ? 1 : -1;
    restartSlideshowInterval();
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
            // No "Loading…" message (owner, D-81): the gallery's highlight moves to the image asked
            // for at once, and the image on screen stays until the new one replaces it.
            if (m_awaitedPath != current) {
                m_awaitedPath = current;
                m_message.clear(); // the earlier request's status
            }
            m_error.clear(); // the image that failed is no longer the one asked for
            updateOverlay();
            // A running decode of this very file is waited for: decodeFinished() comes back here.
            // One of another file (a neighbour preloaded, an image left) is stopped if it runs in a
            // worker process, and waited for if it cannot be stopped (D-93).
            if (!m_decodeBusy)
                startDecode(current, limit, 0);
            else if (m_jobPath != current)
                m_jobCancel->store(true, std::memory_order_relaxed);
            return;
        }
    } else if (!m_awaitedPath.isEmpty()) {
        // Back to the image on screen before the other one arrived (Right, Left).
        m_awaitedPath.clear();
        m_error = m_image.error;
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
    m_jobPath = path;
    m_jobCancel = std::make_shared<std::atomic_bool>(false);
    // An SVG is drawn no larger than the window shows it (D-94).
    m_watcher.setFuture(QtConcurrent::run(&m_decodePool, [path, limit, maxPixels, cancel = m_jobCancel,
                                                          view = deviceSize().toSize()] {
        return decodeImage(path, limit, maxPixels, true, false, cancel.get(), view);
    }));
}

void ViewerWindow::decodeFinished()
{
    // takeResult() moves the image out; result() would copy it and the future would keep
    // its own reference to the pixels alive until the next decode.
    Image image = m_watcher.future().takeResult();
    m_decodeBusy = false;
    if (m_jobCancel->load(std::memory_order_relaxed) && !image.isValid()) { // stopped: no longer wanted
        scheduleWork();
        return;
    }
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
    dropDetail(); // drawn from the image this one replaces
    // The same file again (smaller texture, device loss, language, changed on disk) keeps
    // the view; a new file starts fitted.
    const bool sameFile = image.path == m_image.path;
    if (!image.isValid()) {
        m_message.clear();
        m_error = image.error;
        image.pixels.reset();
        m_image = std::move(image);
        m_renderer.clearImage();
        stopAnimation();
    } else {
        m_message.clear();
        m_error.clear();
        m_renderer.setImage(image.pixels, QSize(image.width, image.height));
        image.pixels.reset(); // the cache keeps them when preloading is on; the GPU has its copy
        m_image = std::move(image);
        if (!sameFile) {
            if (!m_settings.lockZoom) // a locked zoom (Z) carries over to the new image
                m_fit = m_settings.fitMode;
            m_quarterTurns = 0;
            m_mirrored = false;
            resetPan();
            if (m_settings.windowFit == WindowFit::EveryImage
                || (m_settings.windowFit == WindowFit::FirstImage && !m_windowMatched))
                matchWindowToImage();
            m_windowMatched = true;
        }
        clampPan();
        startAnimation(); // or stops the previous one
    }
    m_shownLimit = limit;
    m_imageStale = false;
    m_awaitedPath.clear();
    // Editors often save by replacing the file: watch the path again each time it is shown.
    const QFileInfo shown(m_image.path);
    watchFile(shown.exists() ? m_image.path : QString());
    // Changed while it was being decoded (before the watch above existed): look again.
    if (shown.exists() && (shown.size() != m_image.fileSize || shown.lastModified() != m_image.modified))
        m_folderTimer.start();
    // At either end of a non-looping folder a side button may have nothing left to do.
    setHoverZone(zoneAt(mapFromGlobal(QCursor::pos(screen()))));
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::clearShownImage()
{
    stopAnimation();
    m_image = Image();
    m_renderer.clearImage();
}

void ViewerWindow::watchFile(const QString &path)
{
    const QStringList watched = m_folderWatcher.files(); // the folder is among its directories
    if (!watched.isEmpty())
        m_folderWatcher.removePaths(watched);
    if (!path.isEmpty())
        m_folderWatcher.addPath(path);
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

void ViewerWindow::listFolder(const QString &folder, std::function<void(FolderListing)> done)
{
    const quint64 generation = ++m_listing;
    QtConcurrent::run(&listImages, folder, m_settings.sortBy, m_settings.sortDescending)
        .then(this, [this, generation, done = std::move(done)](FolderListing listing) {
            if (generation == m_listing) // another folder or another order was asked for meanwhile
                done(std::move(listing));
        });
}

void ViewerWindow::relist()
{
    if (m_folder.isEmpty())
        return;
    const QString folder = m_folder;
    listFolder(folder, [this, folder](FolderListing listing) {
        if (folder == m_folder)
            applyListing(std::move(listing));
    });
}

void ViewerWindow::applyListing(FolderListing listing)
{
    QStringList &files = listing.files;
    const QString current = currentPath();
    const int previousIndex = m_index;
    if (!current.isEmpty() && !files.contains(current)) {
        // The name as it was opened may differ in case from the directory entry (Windows, macOS).
        const QString name = QFileInfo(current).fileName();
        const auto same = std::find_if(files.begin(), files.end(), [&name](const QString &f) {
            return QStringView(f).mid(f.lastIndexOf(QLatin1Char('/')) + 1).compare(name, kFileNameCase) == 0;
        });
        if (same != files.end()) {
            listing.stamps.insert(current, listing.stamps.take(*same));
            *same = current;
        } else if (const QFileInfo info(current); info.exists() && info.absolutePath() == m_folder) {
            // A file opened despite an unknown suffix stays in the list while it exists.
            files.prepend(current);
            listing.stamps.insert(current, {info.size(), info.lastModified()});
        }
    }
    m_files = std::move(files);
    m_galleryCentredOn = -2; // the same index may be another file now
    forgetChangedThumbnails(listing.stamps);
    const int index = current.isEmpty() ? -1 : int(m_files.indexOf(current));
    if (index >= 0) {
        m_index = index;
        scheduleWork(); // the neighbours may be different files now
    } else if (m_files.isEmpty()) {
        showFolderEmpty(); // everything was deleted or moved away
        return;
    } else {
        // The current file is gone: the next one takes its place, or the last one.
        m_index = -1;
        restartSlideshowInterval();
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
    relist(); // a listing still running was taken before the removal: it must not bring the file back
    if (m_files.isEmpty()) {
        showFolderEmpty();
        return;
    }
    // Not shown until the next one arrives: the file is gone.
    clearShownImage();
    const int next = std::min(m_index, int(m_files.size()) - 1);
    m_index = -1;
    restartSlideshowInterval();
    startLoading(next);
}

void ViewerWindow::showFolderEmpty()
{
    m_index = -1;
    if (m_slideshow) // paused, its toolbar would stay over the message
        stopSlideshow();
    clearShownImage();
    m_cache.clear();
    m_error = tr("No images left in this folder.");
    setHoverZone(Zone::None);
    updateOverlay();
    requestUpdate();
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

std::optional<ViewerWindow::Detail> ViewerWindow::wantedDetail() const
{
    // An SVG enlarged on screen (its texture is drawn at its own size, or smaller to fit the GPU).
    if (!m_image.vector || m_image.width <= 0 || !m_image.error.isEmpty() || m_image.sourceWidth <= 0
        || m_image.sourceHeight <= 0 || !m_rendererReady)
        return std::nullopt;
    const double zoom = currentZoom();
    if (zoom <= 1.0 + 1e-3)
        return std::nullopt;
    const QRectF shown = imageRect();
    const QRectF visible = shown.intersected(QRectF(QPointF(0, 0), deviceSize()));
    if (visible.isEmpty())
        return std::nullopt;
    // The window's part of the image, in the image's texture coordinates: the view turns the image
    // clockwise by quarter turns after mirroring it (Renderer::recordFrame).
    const int turns = ((m_quarterTurns % 4) + 4) % 4;
    double u0 = 1.0, v0 = 1.0, u1 = 0.0, v1 = 0.0;
    for (const QPointF corner : {visible.topLeft(), visible.bottomRight()}) {
        const double x = (corner.x() - shown.left()) / shown.width(), y = (corner.y() - shown.top()) / shown.height();
        double u = x, v = y;
        switch (turns) {
        case 1: u = y, v = 1.0 - x; break;
        case 2: u = 1.0 - x, v = 1.0 - y; break;
        case 3: u = 1.0 - y, v = x; break;
        default: break;
        }
        if (m_mirrored)
            u = 1.0 - u;
        u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
    }
    // The document at the zoom shown: one of its pixels per device pixel.
    const double scale = shownZoom();
    const double width = m_image.sourceWidth * scale, height = m_image.sourceHeight * scale;
    // On the screen's pixel grid, so that each of its pixels lands on one: the image's left and top
    // are whole device pixels (imageRect()), so an axis the view runs backwards has its pixels from
    // the far edge, a fraction of a pixel off the near one.
    const bool backwardsU = (turns == 2 || turns == 3) != m_mirrored, backwardsV = turns == 1 || turns == 2;
    const double shiftU = backwardsU ? width - std::floor(width) : 0.0;
    const double shiftV = backwardsV ? height - std::floor(height) : 0.0;
    const double left = std::max(std::floor(u0 * width - shiftU), 0.0), top = std::max(std::floor(v0 * height - shiftV), 0.0);
    const double right = std::min(std::ceil(u1 * width - shiftU), std::floor(width - shiftU));
    const double bottom = std::min(std::ceil(v1 * height - shiftV), std::floor(height - shiftV));
    const QRectF area(left + shiftU, top + shiftV, right - left, bottom - top);
    const int maxSide = m_renderer.maxTextureSize();
    if (area.width() < 1.0 || area.height() < 1.0 || area.width() > maxSide || area.height() > maxSide)
        return std::nullopt;
    const QRectF rect(area.x() / width, area.y() / height, area.width() / width, area.height() / height);
    return Detail{m_image.path, scale, area, rect};
}

void ViewerWindow::updateDetail()
{
    std::optional<Detail> wanted = wantedDetail();
    if (wanted == m_detailWanted)
        return; // the same view
    if (m_detailBusy && wanted != m_detailJob)
        m_detailCancel->store(true, std::memory_order_relaxed); // no longer what the view shows
    if (wanted && *wanted != m_detail)
        m_detailTimer.start(); // again: once the view stops changing
    else
        m_detailTimer.stop();
    if (!wanted && !m_detail.path.isEmpty()) { // the image's own pixels are enough: the GPU memory back
        m_detail = {};
        m_renderer.clearDetail();
    }
    m_detailWanted = std::move(wanted);
}

void ViewerWindow::startDetail()
{
    // A running job ends first (stopped if no longer wanted); detailFinished() comes back here.
    if (m_detailBusy || !m_detailWanted || *m_detailWanted == m_detail || *m_detailWanted == m_detailFailed)
        return;
    m_detailBusy = true;
    m_detailJob = *m_detailWanted;
    m_detailCancel = std::make_shared<std::atomic_bool>(false);
    m_detailWatcher.setFuture(QtConcurrent::run(&m_detailPool, [job = m_detailJob, cancel = m_detailCancel] {
        return decodeVectorArea(job.path, job.scale, job.area, cancel.get());
    }));
}

void ViewerWindow::detailFinished()
{
    Image detail = m_detailWatcher.future().takeResult();
    m_detailBusy = false;
    if (m_detailCancel->load(std::memory_order_relaxed)) {
        // stopped: no longer what the view shows
    } else if (detail.isValid() && m_detailJob.path == m_image.path && detail.width == qRound(m_detailJob.area.width())
               && detail.height == qRound(m_detailJob.area.height())) {
        qCInfo(lcNavigation).noquote() << "detail drawn:" << QFileInfo(m_detailJob.path).fileName() << m_detailJob.area
                                       << "at" << m_detailJob.scale << "in" << qRound(detail.decodeMs) << "ms";
        m_renderer.setDetail(detail.pixels, QSize(detail.width, detail.height), m_detailJob.rect);
        m_detail = m_detailJob;
        requestUpdate();
    } else {
        // Past its memory or 30 s, asked for again it would only fail again: the enlarged image stays
        // until the view changes.
        qCInfo(lcNavigation).noquote() << "no detail for" << QFileInfo(m_detailJob.path).fileName() << detail.error;
        m_detailFailed = m_detailJob;
    }
    if (!m_detailTimer.isActive())
        startDetail(); // the view changed while this one was drawn, and has settled
}

void ViewerWindow::dropDetail()
{
    if (m_detailBusy)
        m_detailCancel->store(true, std::memory_order_relaxed);
    m_detailTimer.stop();
    m_detail = {};
    m_detailFailed = {};
    m_detailWanted.reset();
    m_renderer.clearDetail();
}
