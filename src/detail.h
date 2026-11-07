// The zoom's detail of a vector image (D-93 to D-95): an SVG enlarged on screen is drawn again by
// the decode worker at the zoom shown, for the part of it in the window, and replaces the enlarged
// image there; a change of the view asks for another once it settles. Here: which part the view
// wants (vectorDetailFor(), geometry only), and the one job that draws it (VectorDetails). The
// window gives the view and uploads what is drawn; the drawing is handed in (Draw), so that the
// class needs nothing of the decoders and the unit tests drive it with their own
// (tests/unit_tests.cpp). A class of its own since D-102 (it was part of ViewerWindow).
#pragma once

#include "image.h" // Image, PixelBuffer

#include <QFutureWatcher>
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

// How long a view stays before its detail is drawn (VectorDetails::setSettleMs() changes it).
inline constexpr int kDetailSettleMs = 150;

struct VectorDetail {
    QString path;       // the image it is drawn from
    double scale = 0.0; // device pixels per pixel of the document
    QRectF area;        // pixels of the document drawn at `scale`, on the screen's pixel grid
    QRectF rect;        // the same part in the image's texture coordinates
    bool operator==(const VectorDetail &) const = default;
};

// The view of the image on screen, as the window has it.
struct DetailView {
    QString path;
    bool vector = false;         // an SVG (Image::vector)
    bool valid = false;          // decoded, without an error
    QSizeF texture;              // the texture's size (Image::width, height), texels
    QSizeF source;               // the document's own size (Image::sourceWidth, sourceHeight)
    double zoom = 1.0;           // device pixels per texel
    double shownZoom = 1.0;      // device pixels per pixel of the document
    QRectF imageRect;            // device pixels, after the view's rotation
    QSizeF window;               // device pixels
    int quarterTurns = 0;        // clockwise, after mirroring (Renderer::recordFrame)
    bool mirrored = false;
    int maxTextureSize = 0;
};

// The detail the view wants: none unless an SVG is enlarged on screen and its visible part fits
// the GPU's largest texture.
std::optional<VectorDetail> vectorDetailFor(const DetailView &view);

class VectorDetails : public QObject {
    Q_OBJECT

public:
    // Draws `job` (Image with the job's area's pixels, or an error); `cancel` set: it is no longer wanted.
    using Draw = std::function<Image(const VectorDetail &job, const std::atomic_bool *cancel)>;

    explicit VectorDetails(Draw draw, QObject *parent = nullptr);
    ~VectorDetails() override; // a running job is stopped and waited for

    // At every frame: the detail the view wants (none: the image's own pixels are enough). A new
    // one starts once the view has stayed for `settleMs`; a running one no longer wanted is stopped.
    void want(std::optional<VectorDetail> wanted);
    // Another image, or the same one again: nothing drawn, wanted or failed is kept.
    void drop(const QString &imagePath);
    void rename(const QString &path, const QString &target); // the same document, renamed
    void stop(); // the window closes: a running job ends at once and is waited for

    const VectorDetail &shown() const { return m_shown; } // what the renderer has; no path: none
    bool busy() const { return m_busy; }
    void setSettleMs(int ms) { m_timer.setInterval(ms); }

Q_SIGNALS:
    void drawn(const VectorDetail &detail, const Image &image); // upload it: it replaces the enlarged image
    void failed(const VectorDetail &detail, const QString &error); // not asked for again while the view stays
    void cleared(); // the renderer's detail goes

private:
    void start();
    void finished();

    Draw m_draw;
    QThreadPool m_pool; // one thread, beside the decodes
    QFutureWatcher<Image> m_watcher;
    QTimer m_timer;     // a view that has stopped changing gets its detail
    bool m_busy = false;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString m_imagePath;                  // the image shown
    VectorDetail m_job;                   // what the running job draws
    VectorDetail m_shown;                 // what the renderer has; no path: none
    std::optional<VectorDetail> m_wanted; // what the view last asked for
    VectorDetail m_failed;                // not asked for again while the view stays
};
