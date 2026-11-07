// The zoom's detail of a vector image (D-93 to D-95): see detail.h.
#include "detail.h"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

std::optional<VectorDetail> vectorDetailFor(const DetailView &view)
{
    // An SVG enlarged on screen (its texture is drawn at its own size, or smaller to fit the GPU).
    if (!view.vector || !view.valid || view.texture.width() <= 0 || view.source.width() <= 0 || view.source.height() <= 0)
        return std::nullopt;
    const double zoom = view.zoom;
    if (zoom <= 1.0 + 1e-3)
        return std::nullopt;
    const QRectF shown = view.imageRect;
    const QRectF visible = shown.intersected(QRectF(QPointF(0, 0), view.window));
    if (visible.isEmpty())
        return std::nullopt;
    // The window's part of the image, in the image's texture coordinates: the view turns the image
    // clockwise by quarter turns after mirroring it (Renderer::recordFrame).
    const int turns = ((view.quarterTurns % 4) + 4) % 4;
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
        if (view.mirrored)
            u = 1.0 - u;
        u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
    }
    // The document at the zoom shown, from its corner. The detail's pixels are counted on the image as
    // shown, the texture's extent: its document rarely fills its last texel, and counted on the
    // document's sides the detail was squeezed by that fraction, off the screen's pixel grid. That
    // corner is on whole device pixels in every turn and mirror (the window's imageRect()), so whole
    // pixels from it are the screen's.
    const double width = view.texture.width() * zoom, height = view.texture.height() * zoom;
    const double left = std::max(std::floor(u0 * width), 0.0), top = std::max(std::floor(v0 * height), 0.0);
    const double right = std::min(std::ceil(u1 * width), std::floor(width));
    const double bottom = std::min(std::ceil(v1 * height), std::floor(height));
    const QRectF area(left, top, right - left, bottom - top);
    const int maxSide = view.maxTextureSize;
    if (area.width() < 1.0 || area.height() < 1.0 || area.width() > maxSide || area.height() > maxSide)
        return std::nullopt;
    const QRectF rect(area.x() / width, area.y() / height, area.width() / width, area.height() / height);
    return VectorDetail{view.path, view.shownZoom, area, rect};
}

VectorDetails::VectorDetails(Draw draw, QObject *parent) : QObject(parent), m_draw(std::move(draw))
{
    m_pool.setMaxThreadCount(1);
    m_timer.setSingleShot(true);
    m_timer.setInterval(kDetailSettleMs);
    connect(&m_timer, &QTimer::timeout, this, &VectorDetails::start);
    connect(&m_watcher, &QFutureWatcher<Image>::finished, this, &VectorDetails::finished);
}

VectorDetails::~VectorDetails()
{
    stop();
}

void VectorDetails::stop()
{
    m_timer.stop();
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed); // a decode in a worker process ends at once
    m_watcher.waitForFinished();
}

void VectorDetails::want(std::optional<VectorDetail> wanted)
{
    if (wanted == m_wanted)
        return; // the same view
    if (m_busy && wanted != m_job)
        m_cancel->store(true, std::memory_order_relaxed); // no longer what the view shows
    if (wanted && *wanted != m_shown)
        m_timer.start(); // again: once the view stops changing
    else
        m_timer.stop();
    if (!wanted && !m_shown.path.isEmpty()) { // the image's own pixels are enough: the GPU memory back
        m_shown = {};
        Q_EMIT cleared();
    }
    m_wanted = std::move(wanted);
}

void VectorDetails::start()
{
    // A running job ends first (stopped if no longer wanted); finished() comes back here.
    if (m_busy || !m_wanted || *m_wanted == m_shown || *m_wanted == m_failed)
        return;
    m_busy = true;
    m_job = *m_wanted;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_watcher.setFuture(QtConcurrent::run(&m_pool, [draw = m_draw, job = m_job, cancel = m_cancel] {
        return draw(job, cancel.get());
    }));
}

void VectorDetails::finished()
{
    Image detail = m_watcher.future().takeResult();
    m_busy = false;
    if (m_cancel->load(std::memory_order_relaxed)) {
        // stopped: no longer what the view shows
    } else if (detail.isValid() && m_job.path == m_imagePath && detail.width == qRound(m_job.area.width())
               && detail.height == qRound(m_job.area.height())) {
        m_shown = m_job;
        Q_EMIT drawn(m_job, detail);
    } else {
        // Past its memory or 30 s, asked for again it would only fail again: the enlarged image stays
        // until the view changes.
        m_failed = m_job;
        Q_EMIT failed(m_job, detail.error);
    }
    if (!m_timer.isActive())
        start(); // the view changed while this one was drawn, and has settled
}

void VectorDetails::drop(const QString &imagePath)
{
    if (m_busy)
        m_cancel->store(true, std::memory_order_relaxed);
    m_timer.stop();
    m_imagePath = imagePath;
    m_shown = {};
    m_failed = {};
    m_wanted.reset();
    Q_EMIT cleared();
}

void VectorDetails::rename(const QString &path, const QString &target)
{
    // Drawn, wanted, failed or being drawn, it shows the same document.
    for (VectorDetail *detail : {&m_shown, &m_job, &m_failed})
        if (detail->path == path)
            detail->path = target;
    if (m_wanted && m_wanted->path == path)
        m_wanted->path = target;
    if (m_imagePath == path)
        m_imagePath = target;
}
