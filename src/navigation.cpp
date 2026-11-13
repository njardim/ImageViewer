// ViewerWindow, part 2: which image is shown. The folder list and its order, loading from
// the preload cache or the decode thread, preloading the neighbours (decision D-33) and
// following changes on disk.
#include "viewer.h"

#include "decoders.h" // vectorDrawScale() (D-106)
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
            m_gallery.forgetChanged(listing.stamps); // the previous folder's
            m_gallery.recentre(); // another folder: the gallery centres the image again
            m_lastDirectory = folder;
            setFolder(folder);
            startLoading(0);
        });
        return;
    }
    // The image first; the rest of its folder follows from the background listing.
    m_files = {info.absoluteFilePath()};
    m_gallery.recentre();
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

void ViewerWindow::goTo(int index, int direction)
{
    m_direction = direction;
    m_slideshow.restart();
    startLoading(index);
}

void ViewerWindow::step(int delta)
{
    // Every way of stepping (keys, menus, mouse buttons, side zones, slideshow) is offered only
    // where there is a neighbour, as Previous and Next are enabled.
    if (!hasNeighbour(delta))
        return;
    const int n = int(m_files.size());
    goTo(((m_index + delta) % n + n) % n, delta > 0 ? 1 : -1);
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
    // The requested image is on screen. An SVG of it drawn for a smaller window comes first (D-106):
    // a neighbour's decode in a worker process is stopped for it, as for an image asked for.
    const bool redraw = !m_redrawWanted.isEmpty() && !m_redrawTimer.isActive();
    if (redraw && m_decodeBusy && m_jobPath != current)
        m_jobCancel->store(true, std::memory_order_relaxed);
    if (m_decodeBusy || !m_rendererReady)
        return;
    if (redraw) {
        startDecode(current, limit, 0, true);
        return;
    }
    // Then the idle decode thread preloads its neighbours.
    if (!m_settings.preload)
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

void ViewerWindow::startDecode(const QString &path, int limit, qint64 maxPixels, bool redraw)
{
    m_decodeBusy = true;
    m_jobLimit = limit;
    m_jobGeneration = m_decodeGeneration;
    m_jobPath = path;
    m_jobCancel = std::make_shared<std::atomic_bool>(false);
    // An SVG is drawn no larger than the window shows it (D-94).
    const QSize view = vectorWindow(path == m_image.path ? m_quarterTurns : 0); // another image starts upright
    m_jobRedraw = redraw ? view : QSize();
    m_watcher.setFuture(QtConcurrent::run(&m_decodePool, [path, limit, maxPixels, cancel = m_jobCancel, view] {
        return decodeImage(path, limit, maxPixels, true, false, cancel.get(), view);
    }));
}

void ViewerWindow::decodeFinished()
{
    // takeResult() moves the image out; result() would copy it and the future would keep
    // its own reference to the pixels alive until the next decode.
    Image image = m_watcher.future().takeResult();
    m_decodeBusy = false;
    const QSize redrawn = std::exchange(m_jobRedraw, QSize());
    if (m_jobCancel->load(std::memory_order_relaxed) && !image.isValid()) { // stopped: no longer wanted
        scheduleWork();
        return;
    }
    if (m_jobGeneration != m_decodeGeneration) { // decoded for another language: decode again
        scheduleWork();
        return;
    }
    const int limit = m_jobLimit;
    const QString current = currentPath();
    if (!redrawn.isEmpty()) {
        // The SVG on screen drawn again for the window (D-106): it takes the place of the one drawn
        // for a smaller window, the view kept. One that failed, or is no larger, leaves that one on
        // screen and is not asked for again while the window stays (the detail serves, as before).
        if (image.path == current && !needsDisplay(current, limit)) {
            if (image.isValid() && image.documentScale > m_image.documentScale) {
                m_cache.insert(image, limit);
                qCInfo(lcNavigation).noquote() << "drawn again for the window:" << QFileInfo(image.path).fileName()
                                               << QStringLiteral("%1x%2").arg(image.width).arg(image.height);
                showImage(std::move(image), limit, true);
            } else {
                m_redrawFailedPath = image.path;
                m_redrawFailed = redrawn;
                m_redrawWanted = QSize(); // not asked for again below; the next frame asks for the detail
                m_redrawTimer.stop();
                requestUpdate();
                qCInfo(lcNavigation).noquote() << "not drawn again for the window:" << QFileInfo(image.path).fileName()
                                               << (image.error.isEmpty() ? QStringLiteral("no larger") : image.error);
            }
        }
        scheduleWork();
        return;
    }
    m_cache.insert(image, limit); // kept only if it is still the current image or a neighbour
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

void ViewerWindow::showImage(Image image, int limit, bool redrawn)
{
    m_details.drop(image.path); // drawn from the image this one replaces
    m_redrawWanted = QSize(); // worked out again for this image at the next frame (D-106)
    m_redrawTimer.stop();
    if (image.path != m_image.path) { // another image: one that failed may be asked for again
        m_redrawFailedPath.clear();
        m_redrawFailed = QSize();
    }
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
    // A slideshow's interval runs from the image's arrival, not from the step that asked for it; the
    // same image drawn again for the window is no arrival.
    if (!redrawn && !m_slideshow.waitsForScreen())
        m_slideshow.restart();
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
    m_details.drop(QString()); // a detail being drawn has no image to go to
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
    m_gallery.recentre(); // the same index may be another file now
    m_gallery.forgetChanged(listing.stamps);
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
        m_slideshow.restart();
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
    m_slideshow.restart();
    startLoading(next);
}

void ViewerWindow::showFolderEmpty()
{
    m_index = -1;
    if (m_slideshow.running()) // paused, its toolbar would stay over the message
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

void ViewerWindow::updateDetail()
{
    // The view, as the zoom's detail sees it (vectorDetailFor()): computed only for an SVG enlarged
    // on screen.
    DetailView view;
    view.path = m_image.path;
    view.vector = m_image.vector;
    view.valid = m_image.error.isEmpty() && m_rendererReady;
    view.texture = QSizeF(m_image.width, m_image.height);
    view.source = QSizeF(m_image.sourceWidth, m_image.sourceHeight);
    if (view.vector && view.valid && m_image.width > 0 && m_image.height > 0) {
        view.zoom = currentZoom();
        view.shownZoom = shownZoom();
        view.imageRect = imageRect();
    }
    view.window = deviceSize();
    view.quarterTurns = m_quarterTurns;
    view.mirrored = m_mirrored;
    view.maxTextureSize = m_renderer.maxTextureSize();
    std::optional<VectorDetail> wanted = vectorDetailFor(view);
    // Drawn for a smaller window, where drawn for this one it would need no detail, the SVG is drawn
    // again for the window instead, once the window stays (D-106); the detail shown meanwhile stays,
    // and none is started.
    const QSize redraw = wanted ? redrawWindow(view) : QSize();
    if (redraw != m_redrawWanted) {
        if (m_decodeBusy && !m_jobRedraw.isEmpty() && m_jobRedraw != redraw)
            m_jobCancel->store(true, std::memory_order_relaxed); // drawn for a window no longer wanted
        m_redrawWanted = redraw;
        if (redraw.isEmpty())
            m_redrawTimer.stop();
        else
            m_redrawTimer.start();
    }
    if (!redraw.isEmpty()) {
        const VectorDetail &shown = m_details.shown();
        m_details.want(shown.path.isEmpty() ? std::nullopt : std::optional<VectorDetail>(shown));
        return;
    }
    m_details.want(std::move(wanted));
}

QSize ViewerWindow::vectorWindow(int quarterTurns) const
{
    // The document is drawn upright, and shown turned: a quarter turn fits its width in the window's
    // height (D-111). Without it an SVG turned a quarter was drawn for the wrong side of the window,
    // and needed the zoom's detail at every fit.
    const QSize window = deviceSize().toSize();
    return quarterTurns % 2 ? window.transposed() : window;
}

QSize ViewerWindow::redrawWindow(const DetailView &view) const
{
    // The scale svg.cpp would draw the document at for this window, against the scale it was drawn
    // at. The image has the document's sides rounded up: with them the scale is at least what the
    // worker would draw (`fits`), with a pixel less at most (`reach`).
    const QSize window = vectorWindow(view.quarterTurns); // as startDecode() asks
    const int width = m_image.sourceWidth, height = m_image.sourceHeight;
    if (!m_image.vector || !(m_image.documentScale > 0.0) || width <= 0 || height <= 0
        || (m_redrawFailedPath == m_image.path && m_redrawFailed == window))
        return {};
    const double fits = vectorDrawScale(width, height, m_shownLimit, false, window);
    const double reach = vectorDrawScale(std::max(width - 1, 1), std::max(height - 1, 1), m_shownLimit, false, window);
    // Larger by more than the detail's threshold, and enough for the zoom shown.
    if (fits > m_image.documentScale * (1.0 + 1e-3) && view.shownZoom <= reach * (1.0 + 1e-3))
        return window;
    return {};
}

void ViewerWindow::detailDrawn(const VectorDetail &detail, const Image &image)
{
    qCInfo(lcNavigation).noquote() << "detail drawn:" << QFileInfo(detail.path).fileName() << detail.area
                                   << "at" << detail.scale << "in" << qRound(image.decodeMs) << "ms";
    m_renderer.setDetail(image.pixels, QSize(image.width, image.height), detail.rect);
    requestUpdate();
}

void ViewerWindow::detailFailed(const VectorDetail &detail, const QString &error)
{
    qCInfo(lcNavigation).noquote() << "no detail for" << QFileInfo(detail.path).fileName() << error;
}

void ViewerWindow::updateGallery()
{
    if (!m_rendererReady)
        return;
    m_gallery.update({QSizeF(width(), height()), devicePixelRatio(), m_index, m_cache.maxPreloadPixels()});
}

void ViewerWindow::galleryClicked(int slot)
{
    const int index = m_gallery.fileAt(slot);
    if (index < 0 || index == m_index)
        return;
    goTo(index, index > m_index ? 1 : -1);
}

void ViewerWindow::toggleGallery()
{
    savePreference([on = !m_settings.showGallery](Settings &s) { s.showGallery = on; });
    m_gallery.recentre();    // centre on the current image when it shows again
    updateOverlay();         // the information panel moves above the gallery, or back
}
