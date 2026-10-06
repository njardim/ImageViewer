// GPU presentation through QRhi with an SDR or HDR swapchain (decision D-09).
#pragma once

#include <QtGui/qtguiglobal.h>

// Vulkan is used on Linux only (Windows: Direct3D, macOS: Metal).
#if defined(Q_OS_LINUX) && QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
#define IMAGEVIEWER_VULKAN 1
#endif

#include <QFloat16>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

#include <memory>
#include <vector>

class QWindow;
class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiSwapChain;
class QRhiTexture;
class QOffscreenSurface;

class Renderer {
public:
    enum class OutputMode { Sdr = 0, ScRgb = 1, Pq = 2 };

    struct Output {
        OutputMode mode = OutputMode::Sdr;
        float scale = 1.0f; // output units per working unit (1.0 = SDR white)
        float peak = 1.0f;  // brightest representable value, in output units
        QString description;
    };

    // What to draw this frame. Rectangles are in device pixels, origin top-left.
    struct Frame {
        QRectF imageRect;      // where the (oriented) image lands
        int quarterTurns = 0;  // view rotation, clockwise
        bool mirrored = false; // horizontal mirror applied before rotation
        bool nearest = false;  // magnification without interpolation
        float exposure = 1.0f; // linear multiplier
        bool clipWarning = false;
        QRectF overlayRect;    // empty: no overlay
        float background[3] = {0.129f, 0.129f, 0.129f}; // sRGB-encoded
    };

    explicit Renderer(QWindow *window);
    ~Renderer();

    bool initialize(QString *error);
    bool isInitialized() const { return m_rhi != nullptr; }
    void releaseSwapChain();
    // Re-evaluates the swapchain format (e.g. after moving to another screen).
    void refreshOutput();

    // Takes ownership of the pixels (linear scRGB, premultiplied RGBA16F).
    void setImage(std::vector<qfloat16> pixels, QSize size);
    void clearImage();
    void setOverlay(const QImage &overlay); // RGBA8888_Premultiplied, device pixels

    void render(const Frame &frame);

    const Output &output() const { return m_output; }
    QString backendName() const;
    int maxTextureSize() const;

private:
    int chooseFormat() const; // QRhiSwapChain::Format
    bool createSwapChainResources();
    void destroySwapChainResources();
    bool ensureSwapChain();
    void updateOutput();
    bool createPipeline();

    QWindow *m_window;
    std::unique_ptr<QOffscreenSurface> m_fallbackSurface;
    QRhi *m_rhi = nullptr;
    QRhiSwapChain *m_swapChain = nullptr;
    QRhiRenderPassDescriptor *m_renderPass = nullptr;
    QRhiGraphicsPipeline *m_pipeline = nullptr;
    QRhiBuffer *m_vertices = nullptr;
    QRhiBuffer *m_imageUniforms = nullptr;
    QRhiBuffer *m_overlayUniforms = nullptr;
    QRhiSampler *m_linearSampler = nullptr;
    QRhiSampler *m_nearestSampler = nullptr;
    QRhiSampler *m_overlaySampler = nullptr;
    QRhiTexture *m_imageTexture = nullptr;
    QRhiTexture *m_overlayTexture = nullptr;
    QRhiShaderResourceBindings *m_imageBindingsLinear = nullptr;
    QRhiShaderResourceBindings *m_imageBindingsNearest = nullptr;
    QRhiShaderResourceBindings *m_overlayBindings = nullptr;
    QRhiResourceUpdateBatch *m_initialUpdates = nullptr;
    bool m_swapChainReady = false;
    Output m_output;

    std::vector<qfloat16> m_pendingPixels;
    QSize m_pendingSize;
    bool m_imagePending = false;
    bool m_hasImage = false;
    QImage m_pendingOverlay;
    bool m_overlayPending = false;
    bool m_hasOverlay = false;
};
