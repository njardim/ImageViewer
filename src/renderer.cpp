#include "renderer.h"

#include <rhi/qrhi.h>

#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QWindow>

#include <algorithm>
#include <cstring>
#include <utility>

Q_LOGGING_CATEGORY(lcRender, "imageviewer.render")

namespace {

// std140 layout shared with src/shaders/image.{vert,frag}.
struct Uniforms {
    float clipCorrection[16];
    float adjust[4];
    float tone[4];
    float background[4];
    qint32 modes[4];
};
static_assert(sizeof(Uniforms) == 128, "uniform block must match the shaders");

constexpr int kVertexStride = 4 * sizeof(float); // x, y, u, v
constexpr int kQuadBytes = 4 * kVertexStride;
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

// Mirrors color::applyOutputStage(); the PQ terms of the EETF are precomputed here.
void setStage(Uniforms *u, const color::OutputStage &s)
{
    u->adjust[0] = s.exposure;
    u->adjust[1] = s.scale;
    u->adjust[2] = s.peak;
    u->tone[0] = s.nitsPerUnit;
    if (s.sourcePeak > s.peak) {
        u->tone[1] = s.sourcePeak;
        u->tone[2] = color::nitsToPq(s.sourcePeak * s.nitsPerUnit);
        u->tone[3] = color::nitsToPq(s.peak * s.nitsPerUnit) / u->tone[2];
    } else {
        u->tone[1] = 0.0f;
        u->tone[2] = 1.0f;
        u->tone[3] = 1.0f;
    }
    std::copy(s.background, s.background + 3, u->background);
    u->modes[0] = int(s.encoding);
    u->modes[2] = s.clipWarning ? 1 : 0;
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
    out.description = out.peak > 1.0f
                          ? QCoreApplication::translate("Renderer", "EDR · headroom %1×").arg(double(out.peak), 0, 'f', 2)
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
    out.description = QCoreApplication::translate("Renderer", "scRGB · SDR white %1 nits · peak %2 nits")
                          .arg(whiteNits)
                          .arg(peakNits);
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
    out.description = QCoreApplication::translate("Renderer", "HDR10 (PQ) · SDR white %1 nits · peak %2 nits")
                          .arg(whiteNits)
                          .arg(peakNits);
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
    drop(m_placeholderTexture);
    for (OverlaySlot &slot : m_overlays) {
        drop(slot.bindings);
        drop(slot.texture);
        slot.pending = QImage();
        slot.isPending = slot.present = false;
    }
    drop(m_linearSampler);
    drop(m_nearestSampler);
    drop(m_overlaySampler);
    drop(m_vertices);
    drop(m_imageUniforms);
    drop(m_overlayUniforms);
    drop(m_rhi);
    m_fallbackSurface.reset();
    m_pendingPixels = {};
    m_imagePending = m_uploadInFlight = m_hasImage = false;
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
    m_imageUniforms = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms));
    m_overlayUniforms = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms));
    m_linearSampler = m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_nearestSampler = m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_overlaySampler = m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    const bool created = m_vertices->create() && m_imageUniforms->create() && m_overlayUniforms->create()
                         && m_linearSampler->create() && m_nearestSampler->create() && m_overlaySampler->create();
    if (!created) {
        *error = QCoreApplication::translate("Renderer", "Cannot create GPU resources.");
        return false;
    }

    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    m_placeholderTexture = createPlaceholder(m_rhi, QRhiTexture::RGBA16F, updates);
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    auto makeBindings = [&](QRhiBuffer *ubuf, QRhiTexture *texture, QRhiSampler *sampler) {
        QRhiShaderResourceBindings *srb = m_rhi->newShaderResourceBindings();
        srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, ubuf),
                          QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                    texture, sampler)});
        srb->create();
        return srb;
    };
    m_imageBindingsLinear = makeBindings(m_imageUniforms, m_placeholderTexture, m_linearSampler);
    m_imageBindingsNearest = makeBindings(m_imageUniforms, m_placeholderTexture, m_nearestSampler);
    for (OverlaySlot &slot : m_overlays) {
        slot.texture = createPlaceholder(m_rhi, QRhiTexture::RGBA8, updates);
        slot.bindings = makeBindings(m_overlayUniforms, slot.texture, m_overlaySampler);
    }
    m_initialUpdates = updates; // placeholder uploads ride along with the first frame

    if (m_window && !createSwapChainResources()) {
        *error = QCoreApplication::translate("Renderer", "Cannot create the swapchain.");
        return false;
    }
    return true;
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

QRhiGraphicsPipeline *Renderer::createPipeline(QRhiRenderPassDescriptor *renderPass, bool blend)
{
    const QShader vs = loadShader(QStringLiteral(":/shaders/image.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/image.frag.qsb"));
    if (!vs.isValid() || !fs.isValid())
        return nullptr;

    QRhiGraphicsPipeline *pipeline = m_rhi->newGraphicsPipeline();
    QRhiGraphicsPipeline::TargetBlend over; // premultiplied alpha "over"
    over.enable = blend;
    over.srcColor = QRhiGraphicsPipeline::One;
    over.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    over.srcAlpha = QRhiGraphicsPipeline::One;
    over.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline->setTargetBlends({over});
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
        createSwapChainResources();
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

void Renderer::setImage(std::vector<qfloat16> pixels, QSize size)
{
    m_pendingPixels = std::move(pixels);
    m_pendingSize = size;
    m_imagePending = true;
}

void Renderer::clearImage()
{
    m_pendingPixels = {};
    m_imagePending = false;
    m_hasImage = false;
    if (m_imageTexture) { // give the GPU memory back now, not at the next image
        bindImageTexture(m_placeholderTexture);
        m_imageTexture->destroy();
        m_imageTexture->deleteLater();
        m_imageTexture = nullptr;
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

    if (m_imagePending) {
        m_imagePending = false;
        // Free the previous image before allocating the next one, so two large textures never
        // coexist; QRhi defers the native release until the GPU no longer uses it. A fresh
        // object each time: an upload still queued for an old object must never land in it.
        if (m_imageTexture) {
            bindImageTexture(m_placeholderTexture);
            m_imageTexture->destroy();
            m_imageTexture->deleteLater();
            m_imageTexture = nullptr;
        }
        m_hasImage = false;
        QRhiTexture *texture = m_rhi->newTexture(QRhiTexture::RGBA16F, m_pendingSize, 1,
                                                 QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
        if (texture->create()) {
            // No copy: m_pendingPixels stays alive until the frame carrying this batch has ended.
            QRhiTextureSubresourceUploadDescription level0;
            level0.setData(QByteArray::fromRawData(reinterpret_cast<const char *>(m_pendingPixels.data()),
                                                   qsizetype(m_pendingPixels.size() * sizeof(qfloat16))));
            updates->uploadTexture(texture, QRhiTextureUploadDescription({0, 0, level0}));
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
                slot.bindings->setBindings(
                    {QRhiShaderResourceBinding::uniformBuffer(
                         0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                         m_overlayUniforms),
                     QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                               slot.texture, m_overlaySampler)});
                slot.bindings->create();
            }
            updates->uploadTexture(slot.texture, slot.pending);
        }
        slot.pending = QImage();
    }
    return updates;
}

void Renderer::bindImageTexture(QRhiTexture *texture)
{
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    for (QRhiShaderResourceBindings *srb : {m_imageBindingsLinear, m_imageBindingsNearest}) {
        srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, m_imageUniforms),
                          QRhiShaderResourceBinding::sampledTexture(
                              1, QRhiShaderResourceBinding::FragmentStage, texture,
                              srb == m_imageBindingsLinear ? m_linearSampler : m_nearestSampler)});
        srb->create();
    }
}

bool Renderer::takeImageUploadFailure()
{
    return std::exchange(m_imageUploadFailed, false);
}

Renderer::RenderResult Renderer::render(const Frame &frame)
{
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
                takeUpdates(), frame, m_output, clear, nullptr);
    result = m_rhi->endFrame(m_swapChain);
    if (m_uploadInFlight) { // the upload has been submitted; the pixels are no longer needed
        m_pendingPixels = {};
        m_uploadInFlight = false;
    }
    return result == QRhi::FrameOpDeviceLost ? RenderResult::DeviceLost : RenderResult::Done;
}

void Renderer::recordFrame(QRhiCommandBuffer *cb, QRhiRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiResourceUpdateBatch *updates, const Frame &frame, const Output &output,
                           const float clearColour[4], QRhiResourceUpdateBatch *afterPass)
{
    const QSize outputSize = target->pixelSize();
    QMatrix4x4 projection = m_rhi->clipSpaceCorrMatrix();
    projection.ortho(0, float(outputSize.width()), float(outputSize.height()), 0, -1, 1);

    // Image quad: strip order TL, BL, TR, BR. Source corners clockwise: TL, TR, BR, BL.
    const QRectF r = frame.imageRect;
    const float corners[4][2] = {{float(r.left()), float(r.top())}, {float(r.right()), float(r.top())},
                                 {float(r.right()), float(r.bottom())}, {float(r.left()), float(r.bottom())}};
    float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    if (frame.mirrored)
        for (auto &uv : uvs)
            uv[0] = 1.0f - uv[0];
    const int turns = ((frame.quarterTurns % 4) + 4) % 4;
    auto vertex = [&](float *dst, int screenCorner) {
        const int src = (screenCorner - turns + 4) % 4;
        dst[0] = corners[screenCorner][0];
        dst[1] = corners[screenCorner][1];
        dst[2] = uvs[src][0];
        dst[3] = uvs[src][1];
    };
    float vertices[4 * (1 + OverlayLayerCount)][4];
    vertex(vertices[0], 0); // TL
    vertex(vertices[1], 3); // BL
    vertex(vertices[2], 1); // TR
    vertex(vertices[3], 2); // BR
    for (int layer = 0; layer < OverlayLayerCount; ++layer) {
        const QRectF o = frame.overlayRects[layer];
        const float quad[4][4] = {{float(o.left()), float(o.top()), 0, 0},
                                  {float(o.left()), float(o.bottom()), 0, 1},
                                  {float(o.right()), float(o.top()), 1, 0},
                                  {float(o.right()), float(o.bottom()), 1, 1}};
        std::memcpy(vertices[4 * (1 + layer)], quad, sizeof quad);
    }
    updates->updateDynamicBuffer(m_vertices, 0, sizeof vertices, vertices);

    Uniforms u{};
    std::memcpy(u.clipCorrection, projection.constData(), sizeof u.clipCorrection);
    setStage(&u, stageFor(output, frame));
    u.modes[1] = 0;
    updates->updateDynamicBuffer(m_imageUniforms, 0, sizeof u, &u);
    color::OutputStage ui; // the overlay sits at SDR white, never tone mapped
    ui.encoding = output.mode;
    ui.scale = output.scale;
    ui.peak = output.peak;
    ui.nitsPerUnit = output.nitsPerUnit;
    setStage(&u, ui);
    u.modes[1] = 1;
    updates->updateDynamicBuffer(m_overlayUniforms, 0, sizeof u, &u);

    cb->beginPass(target, QColor::fromRgbF(clearColour[0], clearColour[1], clearColour[2], clearColour[3]),
                  {1.0f, 0}, updates);
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    if (m_hasImage && !r.isEmpty()) {
        cb->setShaderResources(frame.nearest ? m_imageBindingsNearest : m_imageBindingsLinear);
        const QRhiCommandBuffer::VertexInput input(m_vertices, 0);
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }
    for (int layer = 0; layer < OverlayLayerCount; ++layer) {
        if (!m_overlays[layer].present || frame.overlayRects[layer].isEmpty())
            continue;
        cb->setShaderResources(m_overlays[layer].bindings);
        const QRhiCommandBuffer::VertexInput input(m_vertices, quint32((1 + layer) * kQuadBytes));
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }
    cb->endPass(afterPass);
}

bool Renderer::renderToBuffer(const Frame &frame, const Output &output, QSize size, std::vector<float> *rgba,
                              QString *error)
{
    if (!m_rhi) {
        *error = QStringLiteral("renderer not initialised");
        return false;
    }
    // Float32 keeps the readback free of quantisation; half float is the fallback.
    const bool full = m_rhi->isTextureFormatSupported(QRhiTexture::RGBA32F);
    std::unique_ptr<QRhiTexture> texture(
        m_rhi->newTexture(full ? QRhiTexture::RGBA32F : QRhiTexture::RGBA16F, size, 1,
                          QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!texture->create()) {
        *error = QStringLiteral("cannot create the offscreen target");
        return false;
    }
    std::unique_ptr<QRhiTextureRenderTarget> target(m_rhi->newTextureRenderTarget({texture.get()}));
    std::unique_ptr<QRhiRenderPassDescriptor> renderPass(target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(renderPass.get());
    if (!target->create()) {
        *error = QStringLiteral("cannot create the offscreen render target");
        return false;
    }
    // No blending: the target starts transparent, and float32 blending is not universal.
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(createPipeline(renderPass.get(), false));
    if (!pipeline) {
        *error = QStringLiteral("cannot create the pipeline");
        return false;
    }

    QRhiCommandBuffer *cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        *error = QStringLiteral("beginOffscreenFrame failed");
        return false;
    }
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch *after = m_rhi->nextResourceUpdateBatch();
    after->readBackTexture(QRhiReadbackDescription(texture.get()), &readback);
    const float transparent[4] = {0, 0, 0, 0};
    recordFrame(cb, target.get(), pipeline.get(), takeUpdates(), frame, output, transparent, after);
    m_rhi->endOffscreenFrame(); // waits for the GPU; the readback is complete afterwards
    m_pendingPixels = {};
    m_uploadInFlight = false;
    if (takeImageUploadFailure()) {
        *error = QStringLiteral("the image does not fit in one GPU texture");
        return false;
    }

    const std::size_t values = std::size_t(size.width()) * size.height() * 4;
    const std::size_t bytes = values * (full ? sizeof(float) : sizeof(qfloat16));
    if (std::size_t(readback.data.size()) != bytes) {
        *error = QStringLiteral("unexpected readback size %1 (expected %2)").arg(readback.data.size()).arg(bytes);
        return false;
    }
    rgba->resize(values);
    if (full)
        std::memcpy(rgba->data(), readback.data.constData(), bytes);
    else
        qFloatFromFloat16(rgba->data(), reinterpret_cast<const qfloat16 *>(readback.data.constData()),
                          qsizetype(values));
    if (m_rhi->isYUpInFramebuffer()) { // OpenGL: rows arrive bottom to top
        const std::size_t row = std::size_t(size.width()) * 4;
        for (int y = 0; y < size.height() / 2; ++y)
            std::swap_ranges(rgba->begin() + y * row, rgba->begin() + (y + 1) * row,
                             rgba->end() - (y + 1) * row);
    }
    return true;
}
