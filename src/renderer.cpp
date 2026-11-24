#include "renderer.h"

#include <rhi/qrhi.h>

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QLoggingCategory>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QWindow>

#include <algorithm>
#include <cstring>
#include <utility>

Q_LOGGING_CATEGORY(lcRender, "imageviewer.render")

namespace {


constexpr float kScRgbUnitNits = 80.0f; // scRGB: 1.0 = 80 cd/m2 (IEC 61966-2-2)

QShader loadShader(const QString &name)
{
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

QRhiTexture *createPlaceholder(QRhi *rhi, QRhiTexture::Format format, QRhiResourceUpdateBatch *updates)
{
    QRhiTexture *texture = rhi->newTexture(format, QSize(1, 1));
    texture->create();
    if (format == QRhiTexture::RGBA16F) {
        const qfloat16 transparent[4] = {};
        updates->uploadTexture(texture, QRhiTextureUploadDescription(
                                            {0, 0, QRhiTextureSubresourceUploadDescription(transparent, sizeof transparent)}));
    } else {
        QImage image(1, 1, QImage::Format_RGBA8888_Premultiplied);
        image.fill(Qt::transparent);
        updates->uploadTexture(texture, image);
    }
    return texture;
}

// How many values an RGBA image of `size` holds; 0 for an empty or invalid size.
std::size_t rgbaValues(QSize size)
{
    return std::size_t(std::max(0, size.width())) * std::size_t(std::max(0, size.height())) * 4;
}

// `pixels` as a texture's first level, read where they are, not copied: the buffer must stay
// alive until the frame that carries the upload has ended.
QRhiTextureUploadDescription firstLevel(const std::vector<qfloat16> &pixels)
{
    QRhiTextureSubresourceUploadDescription level0;
    level0.setData(QByteArray::fromRawData(reinterpret_cast<const char *>(pixels.data()),
                                           qsizetype(pixels.size() * sizeof(qfloat16))));
    return QRhiTextureUploadDescription({0, 0, level0});
}

// Gives a texture's GPU memory back now; QRhi defers the native release until the GPU no longer
// uses it, and the object goes with the frame.
void discard(QRhiTexture *&texture)
{
    texture->destroy();
    texture->deleteLater();
    texture = nullptr;
}

} // namespace

Renderer::Output Renderer::sdrOutput()
{
    Output out;
    out.description = QCoreApplication::translate("Renderer", "SDR (sRGB)");
    return out;
}

Renderer::Output Renderer::edrOutput(float headroom)
{
    Output out;
    out.mode = OutputMode::ScRgb;
    out.peak = std::max(1.0f, headroom);
    //: EDR: Extended Dynamic Range (macOS); %1: how many times SDR white the display can show, e.g. "2.50".
    out.description = out.peak > 1.0f
                          ? QCoreApplication::translate("Renderer", "EDR · headroom %1×").arg(QLocale().toString(double(out.peak), 'f', 2))
                          : QCoreApplication::translate("Renderer", "Linear sRGB managed by ColorSync · no HDR headroom");
    return out;
}

Renderer::Output Renderer::scRgbOutput(float whiteNits, float peakNits)
{
    Output out;
    out.mode = OutputMode::ScRgb;
    out.scale = whiteNits / kScRgbUnitNits;
    out.absoluteScale = color::kSdrReferenceWhiteNits / kScRgbUnitNits;
    out.peak = std::max(out.scale, peakNits / kScRgbUnitNits);
    out.nitsPerUnit = kScRgbUnitNits;
    //: nits: candela per square metre, the unit of luminance.
    out.description = QCoreApplication::translate("Renderer", "scRGB · SDR white %1 nits · peak %2 nits")
                          .arg(QLocale().toString(double(whiteNits), 'f', 0), QLocale().toString(double(peakNits), 'f', 0));
    return out;
}

Renderer::Output Renderer::pqOutput(float whiteNits, float peakNits)
{
    Output out;
    out.mode = OutputMode::Pq;
    out.scale = whiteNits; // the shader works in cd/m2 for PQ
    out.absoluteScale = color::kSdrReferenceWhiteNits;
    out.peak = std::max(whiteNits, peakNits);
    out.nitsPerUnit = 1.0f;
    //: nits: candela per square metre, the unit of luminance.
    out.description = QCoreApplication::translate("Renderer", "HDR10 (PQ) · SDR white %1 nits · peak %2 nits")
                          .arg(QLocale().toString(double(whiteNits), 'f', 0), QLocale().toString(double(peakNits), 'f', 0));
    return out;
}

color::OutputStage Renderer::stageFor(const Output &output, const Frame &frame)
{
    color::OutputStage s;
    s.encoding = output.mode;
    s.exposure = frame.exposure;
    s.scale = frame.absoluteLuminance ? output.absoluteScale : output.scale;
    s.peak = output.peak;
    s.nitsPerUnit = output.nitsPerUnit;
    s.clipWarning = frame.clipWarning;
    // Tone mapping only when the content's luminance exceeds the output: SDR content with
    // wide-gamut components above 1.0 is a gamut matter, not a luminance one, and stays untouched.
    const float k = s.exposure * s.scale;
    if (frame.toneMap && frame.contentLuminancePeak * k > s.peak) // PQ, hence the EETF, ends at 10 000 nits
        s.sourcePeak = std::min(frame.contentPeak * k, color::kPqPeakNits / s.nitsPerUnit);
    for (int c = 0; c < 3; ++c) // the UI grey sits relative to SDR white, like the overlay
        s.background[c] = color::srgbToLinear(frame.background[c]) * output.scale;
    return s;
}

Renderer::Renderer(QWindow *window) : m_window(window) {}

Renderer::~Renderer()
{
    releaseResources();
}

void Renderer::releaseResources()
{
    destroySwapChainResources();
    if (m_initialUpdates)
        m_initialUpdates->release();
    m_initialUpdates = nullptr;
    const auto drop = [](auto *&resource) {
        delete resource;
        resource = nullptr;
    };
    drop(m_imageBindingsLinear);
    drop(m_imageBindingsNearest);
    drop(m_imageTexture);
    drop(m_detailTexture);
    drop(m_placeholderTexture);
    m_boundImage = nullptr;
    for (OverlaySlot &slot : m_overlays) {
        drop(slot.texture);
        slot.pending = QImage();
        slot.isPending = slot.present = false;
    }
    drop(m_linearSampler);
    drop(m_nearestSampler);
    drop(m_detailSampler);
    drop(m_vertices);
    drop(m_imageUniforms);
    drop(m_rhi);
    m_fallbackSurface.reset();
    m_pendingPixels = {};
    m_pendingDetail = {};
    m_imagePending = m_detailPending = m_uploadInFlight = m_hasImage = false;
    m_outputDirty = true;
}

bool Renderer::createRhi()
{
    QSurface::SurfaceType type = QSurface::OpenGLSurface;
    if (m_window) {
        type = m_window->surfaceType();
    } else {
#if defined(Q_OS_WIN)
        type = QSurface::Direct3DSurface;
#elif defined(Q_OS_MACOS)
        type = QSurface::MetalSurface;
#elif defined(IMAGEVIEWER_VULKAN)
        if (m_vulkanInstance)
            type = QSurface::VulkanSurface;
#endif
    }

    switch (type) {
#if defined(Q_OS_WIN)
    case QSurface::Direct3DSurface: {
        QRhiD3D11InitParams params;
        m_rhi = QRhi::create(QRhi::D3D11, &params);
        break;
    }
#endif
#if QT_CONFIG(metal)
    case QSurface::MetalSurface: {
        QRhiMetalInitParams params;
        m_rhi = QRhi::create(QRhi::Metal, &params);
        break;
    }
#endif
#ifdef IMAGEVIEWER_VULKAN
    case QSurface::VulkanSurface: {
        QRhiVulkanInitParams params;
        params.inst = m_window ? m_window->vulkanInstance() : m_vulkanInstance;
        params.window = m_window;
        m_rhi = QRhi::create(QRhi::Vulkan, &params);
        break;
    }
#endif
#if QT_CONFIG(opengl)
    case QSurface::OpenGLSurface: {
        m_fallbackSurface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = m_fallbackSurface.get();
        params.window = m_window;
        m_rhi = QRhi::create(QRhi::OpenGLES2, &params);
        break;
    }
#endif
    default:
        break;
    }
    return m_rhi != nullptr;
}

bool Renderer::initialize(QString *error)
{
    if (!createRhi()) {
        *error = QCoreApplication::translate("Renderer", "Cannot initialize the GPU (QRhi).");
        return false;
    }
    if (!m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F)) {
        *error = QCoreApplication::translate("Renderer", "The GPU does not support RGBA16F textures.");
        return false;
    }
    qCInfo(lcRender) << "backend" << m_rhi->backendName() << m_rhi->driverInfo();

    m_vertices = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, (1 + OverlayLayerCount) * kQuadBytes);
    m_imageUniforms = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBytes);
    m_linearSampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_nearestSampler = m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    // The zoom's detail has no mipmaps: a sampler that reads them would make it incomplete for
    // OpenGL, which then draws it black.
    m_detailSampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    const bool created = m_vertices->create() && m_imageUniforms->create() && m_linearSampler->create() && m_detailSampler->create()
                         && m_nearestSampler->create();
    if (!created) {
        *error = QCoreApplication::translate("Renderer", "Cannot create GPU resources.");
        return false;
    }

    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    m_placeholderTexture = createPlaceholder(m_rhi, QRhiTexture::RGBA16F, updates);
    for (OverlaySlot &slot : m_overlays)
        slot.texture = createPlaceholder(m_rhi, QRhiTexture::RGBA8, updates);
    m_imageBindingsLinear = m_rhi->newShaderResourceBindings();
    m_imageBindingsNearest = m_rhi->newShaderResourceBindings();
    bindImageTexture(m_placeholderTexture);
    m_initialUpdates = updates; // placeholder uploads ride along with the first frame

    if (m_window && !createSwapChainResources()) {
        *error = QCoreApplication::translate("Renderer", "Cannot create the swapchain.");
        return false;
    }
    return true;
}

QSize Renderer::surfaceSize() const
{
    return m_swapChain ? m_swapChain->surfacePixelSize() : QSize();
}

QString Renderer::backendName() const
{
    return m_rhi ? QString::fromLatin1(m_rhi->backendName()) : QString();
}

QString Renderer::deviceName() const
{
    return m_rhi ? QString::fromUtf8(m_rhi->driverInfo().deviceName) : QString();
}

int Renderer::maxTextureSize() const
{
    return m_rhi ? m_rhi->resourceLimit(QRhi::TextureSizeMax) : 8192;
}

// Prefers linear extended sRGB (scRGB / EDR) whenever the screen can show HDR:
// it matches the working space directly. The user's preference (Settings) and,
// above it, IMAGEVIEWER_OUTPUT=sdr|hdr10 (tests, diagnosis) override.
int Renderer::chooseFormat() const
{
    const QByteArray forced = qgetenv("IMAGEVIEWER_OUTPUT").toLower();
    auto supported = [this](QRhiSwapChain::Format f) { return m_swapChain->isFormatSupported(f); };
    if (forced == "sdr" || (forced.isEmpty() && m_outputPreference == OutputPreference::Sdr))
        return QRhiSwapChain::SDR;
    if ((forced == "hdr10" || (forced.isEmpty() && m_outputPreference == OutputPreference::Hdr10))
        && supported(QRhiSwapChain::HDR10))
        return QRhiSwapChain::HDR10;
    // macOS: an SDR layer carries the display's colour space, so sRGB values would reach
    // wide-gamut screens unconverted. The extended linear sRGB tag works on every screen
    // (qrhimetal.mm sets it regardless of isFormatSupported) and ColorSync matches it.
    if (m_rhi->backend() == QRhi::Metal || supported(QRhiSwapChain::HDRExtendedSrgbLinear))
        return QRhiSwapChain::HDRExtendedSrgbLinear;
    if (supported(QRhiSwapChain::HDR10))
        return QRhiSwapChain::HDR10;
    return QRhiSwapChain::SDR;
}

bool Renderer::createSwapChainResources()
{
    m_swapChain = m_rhi->newSwapChain();
    m_swapChain->setWindow(m_window);

    m_swapChain->setFormat(QRhiSwapChain::Format(chooseFormat()));

    m_renderPass = m_swapChain->newCompatibleRenderPassDescriptor();
    m_swapChain->setRenderPassDescriptor(m_renderPass);
    m_swapChainReady = false;
    m_pipeline = createPipeline(m_renderPass);
    return m_pipeline != nullptr;
}

void Renderer::destroySwapChainResources()
{
    delete m_pipeline;
    m_pipeline = nullptr;
    if (m_swapChain)
        m_swapChain->destroy();
    delete m_renderPass;
    m_renderPass = nullptr;
    delete m_swapChain;
    m_swapChain = nullptr;
    m_swapChainReady = false;
}

QRhiGraphicsPipeline *Renderer::createPipeline(QRhiRenderPassDescriptor *renderPass)
{
    const QShader vs = loadShader(QStringLiteral(":/shaders/image.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/image.frag.qsb"));
    if (!vs.isValid() || !fs.isValid())
        return nullptr;

    // No blending: overlay quads composite in the shader, over the image worked out again (D-49).
    QRhiGraphicsPipeline *pipeline = m_rhi->newGraphicsPipeline();
    pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    QRhiVertexInputLayout layout;
    layout.setBindings({{kVertexStride}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                          {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
    pipeline->setVertexInputLayout(layout);
    pipeline->setShaderResourceBindings(m_imageBindingsLinear);
    pipeline->setRenderPassDescriptor(renderPass);
    if (!pipeline->create()) {
        delete pipeline;
        return nullptr;
    }
    return pipeline;
}

void Renderer::releaseSwapChain()
{
    // Unconditional: after an out-of-date frame the native swapchain still exists
    // even though it is not "ready", and it must not outlive its surface.
    if (m_swapChain)
        m_swapChain->destroy();
    m_swapChainReady = false;
}

void Renderer::refreshOutput()
{
    if (!m_rhi || !m_swapChain)
        return;
    if (chooseFormat() != int(m_swapChain->format())) {
        destroySwapChainResources();
        if (!createSwapChainResources()) // render() then reports a lost device: the viewer builds it all again
            qCWarning(lcRender) << "cannot create the swapchain's pipeline for the new format";
    } else if (m_rhi->backend() == QRhi::Metal) {
        // AppKit re-assigns the layer's colour space on screen and profile changes;
        // createOrResize() puts the extended linear sRGB tag back.
        m_swapChainReady = false;
    }
    m_outputDirty = true; // same format, but SDR white or peak may have changed
    m_window->requestUpdate();
}

bool Renderer::ensureSwapChain()
{
    if (!m_swapChainReady || m_swapChain->currentPixelSize() != m_swapChain->surfacePixelSize()) {
        if (m_swapChain->surfacePixelSize().isEmpty())
            return false;
        m_swapChainReady = m_swapChain->createOrResize();
        if (!m_swapChainReady)
            return false;
    }
    // hdrInfo() can be expensive (DXGI output enumeration): read it when the format or
    // screen changes, not on every resize. The EDR headroom on macOS is the exception:
    // it is cheap, is often wrong on the first frame and follows the brightness slider.
    if (m_outputDirty || m_rhi->backend() == QRhi::Metal) {
        updateOutput();
        m_outputDirty = false;
    }
    return true;
}

bool Renderer::outputIsMeasured() const
{
    // EDR headroom comes from AppKit; nits and SDR white only from DXGI on Windows.
    // Everywhere else Qt reports fixed defaults (1000 nits peak, SDR white 200).
    const QRhi::Implementation backend = m_rhi->backend();
    return m_swapChain->hdrInfo().limitsType == QRhiSwapChainHdrInfo::ColorComponentValue
               ? m_swapChain->format() != QRhiSwapChain::HDR10
               : backend == QRhi::D3D11 || backend == QRhi::D3D12;
}

void Renderer::updateOutput()
{
    const QRhiSwapChainHdrInfo info = m_swapChain->hdrInfo();
    const float peakNits = info.limitsType == QRhiSwapChainHdrInfo::LuminanceInNits
                                   && info.limits.luminanceInNits.maxLuminance > 0
                               ? info.limits.luminanceInNits.maxLuminance
                               : 1000.0f;
    Output out;
    switch (m_swapChain->format()) {
    case QRhiSwapChain::HDRExtendedSrgbLinear:
        if (info.limitsType == QRhiSwapChainHdrInfo::ColorComponentValue)
            out = edrOutput(info.limits.colorComponentValue.maxColorComponentValue);
        else // Windows: SDR white follows the user's SDR brightness setting
            out = scRgbOutput(info.sdrWhiteLevel > 0 ? info.sdrWhiteLevel : kScRgbUnitNits, peakNits);
        break;
    case QRhiSwapChain::HDR10:
        out = pqOutput(info.sdrWhiteLevel > 0 ? info.sdrWhiteLevel : color::kSdrReferenceWhiteNits, peakNits);
        break;
    case QRhiSwapChain::SDR:
    default:
        out = sdrOutput();
        break;
    }
    if (out.mode != OutputMode::Sdr && !outputIsMeasured())
        out.description += QLatin1Char(' ') + QCoreApplication::translate("Renderer", "(Qt defaults, not measured)");
    if (out.description != m_output.description)
        qCInfo(lcRender) << "output" << out.description << info;
    m_output = out;
}

void Renderer::setImage(std::shared_ptr<const std::vector<qfloat16>> pixels, QSize size)
{
    clearDetail(); // drawn from the image it replaces
    m_pendingPixels = std::move(pixels);
    m_pendingSize = size;
    m_imagePending = true;
    m_frameUpdate = false;
}

void Renderer::setDetail(std::shared_ptr<const std::vector<qfloat16>> pixels, QSize size, QRectF rect)
{
    m_pendingDetail = std::move(pixels);
    m_pendingDetailSize = size;
    m_pendingDetailRect = rect;
    m_detailPending = true;
}

void Renderer::clearDetail()
{
    m_pendingDetail = {};
    m_detailPending = false;
    if (m_detailTexture) { // the GPU memory back now
        discard(m_detailTexture);
        bindImageTexture(m_boundImage);
    }
}

void Renderer::setFrame(std::shared_ptr<const std::vector<qfloat16>> pixels, QSize size)
{
    const bool replacesImage = m_imagePending && !m_frameUpdate; // a new image still waits: keep its path
    setImage(std::move(pixels), size);
    m_frameUpdate = !replacesImage;
}

void Renderer::clearImage()
{
    clearDetail();
    m_pendingPixels = {};
    m_imagePending = false;
    m_hasImage = false;
    if (m_imageTexture) { // give the GPU memory back now, not at the next image
        bindImageTexture(m_placeholderTexture);
        discard(m_imageTexture);
    }
}

void Renderer::setOverlay(int layer, const QImage &overlay)
{
    if (layer < 0 || layer >= OverlayLayerCount)
        return;
    m_overlays[layer].pending = overlay;
    m_overlays[layer].isPending = true;
}

void Renderer::setOutputPreference(OutputPreference preference)
{
    if (preference == m_outputPreference)
        return;
    m_outputPreference = preference;
    refreshOutput();
}

QRhiResourceUpdateBatch *Renderer::takeUpdates()
{
    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    if (m_initialUpdates) {
        updates->merge(m_initialUpdates);
        m_initialUpdates->release();
        m_initialUpdates = nullptr;
    }

    const std::size_t pendingValues = rgbaValues(m_pendingSize);
    if (m_imagePending && m_frameUpdate && m_imageTexture && m_hasImage && m_imageTexture->pixelSize() == m_pendingSize
        && m_pendingPixels && m_pendingPixels->size() == pendingValues) {
        // Another frame of the same animation: the texture stays, only its contents change.
        m_imagePending = m_frameUpdate = false;
        updates->uploadTexture(m_imageTexture, firstLevel(*m_pendingPixels));
        updates->generateMips(m_imageTexture);
        m_uploadInFlight = true;
    }
    if (m_imagePending) {
        m_imagePending = m_frameUpdate = false;
        // Free the previous image before allocating the next one, so two large textures never
        // coexist; QRhi defers the native release until the GPU no longer uses it. A fresh
        // object each time: an upload still queued for an old object must never land in it.
        if (m_imageTexture) {
            bindImageTexture(m_placeholderTexture);
            discard(m_imageTexture);
        }
        m_hasImage = false;
        QRhiTexture *texture = m_rhi->newTexture(QRhiTexture::RGBA16F, m_pendingSize, 1,
                                                 QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
        if (m_pendingPixels && m_pendingPixels->size() == pendingValues && pendingValues > 0 && texture->create()) {
            // No copy: m_pendingPixels stays alive until the frame carrying this batch has ended.
            updates->uploadTexture(texture, firstLevel(*m_pendingPixels));
            updates->generateMips(texture);
            m_imageTexture = texture;
            bindImageTexture(texture);
            m_hasImage = true;
            m_uploadInFlight = true;
        } else {
            // Typically larger than the device allows for one resource; the viewer retries
            // at a lower resolution.
            qCWarning(lcRender) << "cannot create a texture of" << m_pendingSize;
            delete texture;
            m_imageUploadFailed = true;
            m_pendingPixels = {};
        }
    }

    if (m_detailPending) {
        // As a new image: the old texture goes first, a fresh object takes the pixels. No mipmaps:
        // the detail is drawn at the scale shown, or larger while the next one is drawn.
        m_detailPending = false;
        if (m_detailTexture)
            discard(m_detailTexture);
        QRhiTexture *texture = m_rhi->newTexture(QRhiTexture::RGBA16F, m_pendingDetailSize);
        const std::size_t expected = rgbaValues(m_pendingDetailSize);
        if (m_pendingDetail && m_pendingDetail->size() == expected && expected > 0 && texture->create()) {
            updates->uploadTexture(texture, firstLevel(*m_pendingDetail));
            m_detailTexture = texture;
            m_detailRect = m_pendingDetailRect;
            m_uploadInFlight = true; // m_pendingDetail stays alive until the frame has ended
        } else {
            qCWarning(lcRender) << "cannot create a detail texture of" << m_pendingDetailSize;
            delete texture;
            m_pendingDetail = {};
        }
        bindImageTexture(m_boundImage);
    }

    bool newOverlayTexture = false;
    for (OverlaySlot &slot : m_overlays) {
        if (!slot.isPending)
            continue;
        slot.isPending = false;
        slot.present = !slot.pending.isNull();
        if (slot.present) {
            if (slot.texture->pixelSize() != slot.pending.size()) {
                slot.texture->deleteLater();
                slot.texture = m_rhi->newTexture(QRhiTexture::RGBA8, slot.pending.size());
                slot.texture->create();
                newOverlayTexture = true;
            }
            updates->uploadTexture(slot.texture, slot.pending);
        }
        slot.pending = QImage();
    }
    if (newOverlayTexture)
        bindImageTexture(m_boundImage);
    return updates;
}

void Renderer::bindImageTexture(QRhiTexture *texture)
{
    // One set for the image and the overlay quads: these read the image too (image.frag).
    m_boundImage = texture;
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    for (QRhiShaderResourceBindings *srb : {m_imageBindingsLinear, m_imageBindingsNearest}) {
        QList<QRhiShaderResourceBinding> bindings = {
            QRhiShaderResourceBinding::uniformBuffer(0, stages, m_imageUniforms),
            QRhiShaderResourceBinding::sampledTexture(1, fragment, texture,
                                                      srb == m_imageBindingsLinear ? m_linearSampler : m_nearestSampler)};
        for (int layer = 0; layer < OverlayLayerCount; ++layer)
            bindings.append(QRhiShaderResourceBinding::sampledTexture(2 + layer, fragment, m_overlays[layer].texture,
                                                                      m_nearestSampler)); // sampled 1:1
        bindings.append(QRhiShaderResourceBinding::sampledTexture(
            2 + OverlayLayerCount, fragment, m_detailTexture ? m_detailTexture : m_placeholderTexture, m_detailSampler));
        srb->setBindings(bindings.cbegin(), bindings.cend());
        srb->create();
    }
}

bool Renderer::takeImageUploadFailure()
{
    return std::exchange(m_imageUploadFailed, false);
}

Renderer::RenderResult Renderer::render(const Frame &frame)
{
    if (m_rhi && m_swapChain && !m_pipeline) // a new swapchain format left without a pipeline (refreshOutput())
        return RenderResult::DeviceLost;
    if (!m_rhi || !m_swapChain || !ensureSwapChain())
        return RenderResult::NotReady;

    QRhi::FrameOpResult result = m_rhi->beginFrame(m_swapChain);
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        m_swapChainReady = false;
        if (!ensureSwapChain())
            return RenderResult::NotReady;
        result = m_rhi->beginFrame(m_swapChain);
    }
    if (result == QRhi::FrameOpDeviceLost)
        return RenderResult::DeviceLost;
    if (result != QRhi::FrameOpSuccess) {
        qCWarning(lcRender) << "beginFrame failed" << result;
        return RenderResult::NotReady;
    }

    // Background: the UI grey at SDR white, encoded like everything else for the output.
    const color::OutputStage stage = stageFor(m_output, frame);
    float clear[4] = {stage.background[0], stage.background[1], stage.background[2], 1.0f};
    color::encodeOutput(m_output.mode, clear);
    recordFrame(m_swapChain->currentFrameCommandBuffer(), m_swapChain->currentFrameRenderTarget(), m_pipeline,
                takeUpdates(), frame, m_output, clear, stage.background, nullptr);
    result = m_rhi->endFrame(m_swapChain);
    if (m_uploadInFlight) { // the upload has been submitted; the pixels are no longer needed
        m_pendingPixels = {};
        m_pendingDetail = {};
        m_uploadInFlight = false;
    }
    return result == QRhi::FrameOpDeviceLost ? RenderResult::DeviceLost : RenderResult::Done;
}
