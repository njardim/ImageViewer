// The main window: a QWindow presented through QRhi so it can use an HDR
// swapchain (decision D-09). Owns navigation, view state, input and overlays.
#pragma once

#include "image.h"
#include "renderer.h"

#include <QFutureWatcher>
#include <QThreadPool>
#include <QPointF>
#include <QStringList>
#include <QWindow>

class QVulkanInstance;

class ViewerWindow : public QWindow {
    Q_OBJECT

public:
    explicit ViewerWindow(QVulkanInstance *vulkan = nullptr);
    ~ViewerWindow() override;

    void openFile(const QString &path);

protected:
    bool event(QEvent *e) override;
    void exposeEvent(QExposeEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void initializeRenderer();
    void render();
    void startLoading(int index);
    void imageDecoded();
    void step(int delta);
    void showOpenDialog();
    void showContextMenu(const QPoint &globalPos);
    void toggleFullScreen();

    QSizeF deviceSize() const;
    QSizeF displayedImageSize() const; // after view rotation
    double fitZoom() const;
    double currentZoom() const;
    QRectF imageRect() const;
    void zoomAt(double factor, const QPointF &devicePos);
    void setActualSize();
    void setFit();
    void clampPan();
    void updateOverlay();
    Renderer::Frame imageFrame() const; // colour-related fields of the current frame
    int textureLimit(const QString &path) const; // longest side the GPU texture may have
    void recoverFromDeviceLoss();

    // View actions, shared by the keyboard and the context menu.
    void rotate(int quarterTurns);
    void toggleMirror();
    void adjustExposure(float ev);
    void resetExposure();
    void toggleToneMap();
    void toggleClipWarning();
    void toggleInfo();

    Renderer m_renderer;
    bool m_rendererReady = false;
    bool m_rendererFailed = false;

    QStringList m_files;
    int m_index = -1;
    int m_pendingIndex = -1; // requested while a decode was running; starts when it ends
    QString m_textureCapPath; // the file whose upload the GPU refused...
    int m_textureCap = 0;     // ...and the size that file is decoded at now
    // Decodes run on their own thread: decodeImage() itself fans out on the global pool
    // with blockingMap, which deadlocks if the decode occupies the pool's only thread.
    QThreadPool m_decodePool;
    QFutureWatcher<Image> m_watcher;
    Image m_image; // metadata of the displayed image (pixels live on the GPU)
    QString m_message;

    bool m_fit = true;
    double m_zoom = 1.0; // device pixels per image pixel when not fitting
    QPointF m_pan;       // offset of the image centre from the window centre, device pixels
    int m_quarterTurns = 0;
    bool m_mirrored = false;
    float m_exposureEv = 0.0f;
    bool m_clipWarning = false;
    bool m_toneMap = true;
    bool m_showInfo = true;
    QSize m_overlaySize; // device pixels; empty when no overlay is shown
    QString m_overlayOutput; // output description the overlay was built with

    bool m_dragging = false;
    QPointF m_dragOrigin;
    QPointF m_panOrigin;
};
