// GPU presentation through QRhi with an SDR or HDR swapchain (decision D-09).
#pragma once

#include "color.h"

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

#include <array>
#include <memory>
#include <vector>

class QWindow;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiSwapChain;
class QRhiTexture;
class QOffscreenSurface;
class QVulkanInstance;

class Renderer {
public:
    using OutputMode = color::OutputEncoding;

    struct Output {
        OutputMode mode = OutputMode::Sdr;
        float scale = 1.0f;         // output units per working unit for relative content (1.0 = SDR white)
        float absoluteScale = 1.0f; // same, for absolute-luminance content (PQ): 203 nits per working unit
        float peak = 1.0f;          // brightest representable value, in output units
        float nitsPerUnit = color::kSdrReferenceWhiteNits; // luminance of one output unit (EETF domain)
        QString description;
    };

    // Output parameters per swapchain kind. Nits are cd/m2.
    static Output sdrOutput();
    static Output edrOutput(float headroom);                // macOS: 1.0 = SDR white, relative only
    static Output scRgbOutput(float whiteNits, float peakNits); // Windows: 1.0 = 80 nits
    static Output pqOutput(float whiteNits, float peakNits);    // HDR10, output units are nits

    // UI layers drawn over the image at SDR white, each from its own small texture so
    // that changing one (hover feedback) does not re-upload the others.
    enum OverlayLayer { InfoLayer, TopLayer, PreviousButtonLayer, NextButtonLayer, OverlayLayerCount };

    // Swapchain choice; IMAGEVIEWER_OUTPUT=sdr|hdr10 still overrides it (tests, diagnosis).
    enum class OutputPreference { Automatic, Sdr, Hdr10 };

    // What to draw this frame. Rectangles are in device pixels, origin top-left.
    struct Frame {
        QRectF imageRect;      // where the (oriented) image lands
        int quarterTurns = 0;  // view rotation, clockwise
        bool mirrored = false; // horizontal mirror applied before rotation
        bool nearest = false;  // magnification without interpolation
        float exposure = 1.0f; // linear multiplier
        bool toneMap = true;   // BT.2390 EETF when the content exceeds the output peak; otherwise clip
        bool clipWarning = false;
        float contentPeak = 0.0f;          // brightest RGB component, working units
        float contentLuminancePeak = 0.0f; // brightest luminance, working units
        bool absoluteLuminance = false;    // PQ content: keep absolute nits where the output allows it
        QRectF overlayRects[OverlayLayerCount]; // empty: layer not drawn
        float background[3] = {0.129f, 0.129f, 0.129f}; // sRGB-encoded
        // Checks behind translucent image pixels, anchored to the image: `checkerCells` is the
        // number of cells along the texture's width and height; every other cell uses
        // `checkerColour` (sRGB-encoded) instead of the background. Off in the harness.
        bool checkerboard = false;
        float checkerCells[2] = {0.0f, 0.0f};
        float checkerColour[3] = {0.0f, 0.0f, 0.0f};
    };

    // The output-stage parameters the shader receives for the image layer.
    static color::OutputStage stageFor(const Output &output, const Frame &frame);

    enum class RenderResult { Done, NotReady, DeviceLost };

    // `window` may be null for offscreen use (renderToBuffer).
    explicit Renderer(QWindow *window);
    ~Renderer();

    // Offscreen Vulkan needs an instance; windows bring their own.
    void setVulkanInstance(QVulkanInstance *instance) { m_vulkanInstance = instance; }
    bool initialize(QString *error);
    // Drops every GPU object and the QRhi itself; initialize() may be called again
    // (recovery after RenderResult::DeviceLost). Pending pixels are discarded too.
    void releaseResources();
    void releaseSwapChain();
    // Re-evaluates the swapchain format (e.g. after moving to another screen).
    void refreshOutput();

    // Linear scRGB, premultiplied RGBA16F. The buffer is shared (preload cache, decision D-33)
    // and only read; the renderer keeps its reference until the upload has been submitted.
    void setImage(std::shared_ptr<const std::vector<qfloat16>> pixels, QSize size);
    // Another frame of the image shown (same size): uploaded into the same texture.
    void setFrame(std::shared_ptr<const std::vector<qfloat16>> pixels, QSize size);
    void clearImage();
    void setOverlay(int layer, const QImage &overlay); // RGBA8888_Premultiplied, device pixels; null hides
    void setOutputPreference(OutputPreference preference);

    RenderResult render(const Frame &frame);
    // True once after an image could not be put on the GPU (e.g. larger than the
    // device allows); the image is then not shown.
    bool takeImageUploadFailure();

    // Fidelity harness: draws `frame` with `output` into a float target of `size`
    // cleared to transparent black and reads it back (RGBA, rows top to bottom).
    bool renderToBuffer(const Frame &frame, const Output &output, QSize size, std::vector<float> *rgba,
                        QString *error);

    const Output &output() const { return m_output; }
    QString backendName() const;
    QString deviceName() const;
    int maxTextureSize() const;

private:
    bool createRhi();
    int chooseFormat() const; // QRhiSwapChain::Format
    bool createSwapChainResources();
    void destroySwapChainResources();
    bool ensureSwapChain();
    void updateOutput();
    bool outputIsMeasured() const; // hdrInfo comes from the OS, not Qt's built-in defaults
    QRhiGraphicsPipeline *createPipeline(QRhiRenderPassDescriptor *renderPass, bool blend = true);
    QRhiResourceUpdateBatch *takeUpdates(); // pending uploads for this frame
    void bindImageTexture(QRhiTexture *texture);
    void recordFrame(QRhiCommandBuffer *cb, QRhiRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                     QRhiResourceUpdateBatch *updates, const Frame &frame, const Output &output,
                     const float clearColour[4], QRhiResourceUpdateBatch *afterPass);

    QWindow *m_window;
    QVulkanInstance *m_vulkanInstance = nullptr;
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
    QRhiTexture *m_placeholderTexture = nullptr; // 1x1 transparent, bound while there is no image
    QRhiTexture *m_imageTexture = nullptr;       // null when there is no image
    QRhiShaderResourceBindings *m_imageBindingsLinear = nullptr;
    QRhiShaderResourceBindings *m_imageBindingsNearest = nullptr;
    struct OverlaySlot {
        QRhiTexture *texture = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        QImage pending;
        bool isPending = false;
        bool present = false;
    };
    std::array<OverlaySlot, OverlayLayerCount> m_overlays;
    OutputPreference m_outputPreference = OutputPreference::Automatic;
    QRhiResourceUpdateBatch *m_initialUpdates = nullptr;
    bool m_swapChainReady = false;
    bool m_outputDirty = true; // hdrInfo must be read again
    Output m_output;

    std::shared_ptr<const std::vector<qfloat16>> m_pendingPixels;
    QSize m_pendingSize;
    bool m_imagePending = false;
    bool m_uploadInFlight = false; // m_pendingPixels back an upload until the frame ends
    bool m_hasImage = false;
    bool m_frameUpdate = false; // the pending pixels are a new frame for the current texture
    bool m_imageUploadFailed = false;
};
