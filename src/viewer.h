// The main window: a QWindow presented through QRhi so it can use an HDR
// swapchain (decision D-09). Owns navigation, view state, input and overlays.
#pragma once

#include "image.h"
#include "renderer.h"

#include <QFutureWatcher>
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

    Renderer m_renderer;
    bool m_rendererReady = false;
    bool m_rendererFailed = false;

    QStringList m_files;
    int m_index = -1;
    QString m_loadingPath;
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
    bool m_showInfo = true;
    QSize m_overlaySize; // device pixels; empty when no overlay is shown

    bool m_dragging = false;
    QPointF m_dragOrigin;
    QPointF m_panOrigin;
};
