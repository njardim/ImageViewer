#include "renderer.h"

#include <rhi/qrhi.h>

#include <QFile>
#include <QLoggingCategory>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QWindow>

#include <algorithm>
#include <cstring>

Q_LOGGING_CATEGORY(lcRender, "imageviewer.render")

namespace {

// std140 layout shared with src/shaders/image.{vert,frag}.
struct Uniforms {
    float clipCorrection[16];
    float adjust[4];
    float tone[4];
    qint32 modes[4];
};
static_assert(sizeof(Uniforms) == 112, "uniform block must match the shaders");

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
    u->modes[0] = int(s.encoding);
    u->modes[2] = s.clipWarning ? 1 : 0;
}

} // namespace

Renderer::Output Renderer::sdrOutput()
{
    Output out;
    out.description = QStringLiteral("SDR (sRGB)");
    return out;
}

Renderer::Output Renderer::edrOutput(float headroom)
{
    Output out;
    out.mode = OutputMode::ScRgb;
    out.peak = std::max(1.0f, headroom);
    out.description = QStringLiteral("EDR · headroom %1×").arg(double(out.peak), 0, 'f', 2);
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
    out.description = QStringLiteral("scRGB · branco SDR %1 nits · pico %2 nits").arg(whiteNits).arg(peakNits);
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
    out.description = QStringLiteral("HDR10 (PQ) · branco SDR %1 nits · pico %2 nits").arg(whiteNits).arg(peakNits);
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
    if (frame.toneMap && frame.contentLuminancePeak * k > s.peak)
        s.sourcePeak = frame.contentPeak * k;
    return s;
}

Renderer::Renderer(QWindow *window) : m_window(window) {}

Renderer::~Renderer()
{
    destroySwapChainResources();
    if (m_initialUpdates)
        m_initialUpdates->release();
    for (QRhiResource *r : std::initializer_list<QRhiResource *>{
             m_imageBindingsLinear, m_imageBindingsNearest, m_overlayBindings, m_imageTexture, m_overlayTexture,
             m_linearSampler, m_nearestSampler, m_overlaySampler, m_vertices, m_imageUniforms, m_overlayUniforms})
        delete r;
    delete m_swapChain;
    delete m_rhi;
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
        *error = QStringLiteral("Não foi possível inicializar a GPU (QRhi).");
        return false;
    }
    if (!m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F)) {
        *error = QStringLiteral("A GPU não suporta texturas RGBA16F.");
        return false;
    }
    qCInfo(lcRender) << "backend" << m_rhi->backendName() << m_rhi->driverInfo();

    m_vertices = m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 2 * kQuadBytes);
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
        *error = QStringLiteral("Falha ao criar recursos da GPU.");
        return false;
    }

    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
    m_imageTexture = createPlaceholder(m_rhi, QRhiTexture::RGBA16F, updates);
    m_overlayTexture = createPlaceholder(m_rhi, QRhiTexture::RGBA8, updates);
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    auto makeBindings = [&](QRhiBuffer *ubuf, QRhiTexture *texture, QRhiSampler *sampler) {
        QRhiShaderResourceBindings *srb = m_rhi->newShaderResourceBindings();
        srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, ubuf),
                          QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                    texture, sampler)});
        srb->create();
        return srb;
    };
    m_imageBindingsLinear = makeBindings(m_imageUniforms, m_imageTexture, m_linearSampler);
    m_imageBindingsNearest = makeBindings(m_imageUniforms, m_imageTexture, m_nearestSampler);
    m_overlayBindings = makeBindings(m_overlayUniforms, m_overlayTexture, m_overlaySampler);
    m_initialUpdates = updates; // placeholder uploads ride along with the first frame

    if (m_window && !createSwapChainResources()) {
        *error = QStringLiteral("Falha ao criar a swapchain.");
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
// it matches the working space directly. IMAGEVIEWER_OUTPUT=sdr|hdr10 overrides.
int Renderer::chooseFormat() const
{
    const QByteArray forced = qgetenv("IMAGEVIEWER_OUTPUT").toLower();
    auto supported = [this](QRhiSwapChain::Format f) { return m_swapChain->isFormatSupported(f); };
    if (forced == "sdr")
        return QRhiSwapChain::SDR;
    if (forced == "hdr10" && supported(QRhiSwapChain::HDR10))
        return QRhiSwapChain::HDR10;
    if (supported(QRhiSwapChain::HDRExtendedSrgbLinear))
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
    if (m_swapChainReady) {
        m_swapChain->destroy();
        m_swapChainReady = false;
    }
}

void Renderer::refreshOutput()
{
    if (!m_rhi || !m_swapChain)
        return;
    if (chooseFormat() != int(m_swapChain->format())) {
        destroySwapChainResources();
        createSwapChainResources();
    } else if (m_swapChainReady) {
        updateOutput(); // same format, but SDR white or peak may have changed
    }
    m_window->requestUpdate();
}

bool Renderer::ensureSwapChain()
{
    if (m_swapChainReady && m_swapChain->currentPixelSize() == m_swapChain->surfacePixelSize())
        return true;
    if (m_swapChain->surfacePixelSize().isEmpty())
        return false;
    m_swapChainReady = m_swapChain->createOrResize();
    if (m_swapChainReady)
        updateOutput();
    return m_swapChainReady;
}

void Renderer::updateOutput()
{
    const QRhiSwapChainHdrInfo info = m_swapChain->hdrInfo();
    const float peakNits = info.limitsType == QRhiSwapChainHdrInfo::LuminanceInNits
                                   && info.limits.luminanceInNits.maxLuminance > 0
                               ? info.limits.luminanceInNits.maxLuminance
                               : 1000.0f;
    switch (m_swapChain->format()) {
    case QRhiSwapChain::HDRExtendedSrgbLinear:
    case QRhiSwapChain::HDRExtendedDisplayP3Linear:
        if (info.limitsType == QRhiSwapChainHdrInfo::ColorComponentValue)
            m_output = edrOutput(info.limits.colorComponentValue.maxColorComponentValue);
        else // Windows: SDR white follows the user's SDR brightness setting
            m_output = scRgbOutput(info.sdrWhiteLevel > 0 ? info.sdrWhiteLevel : kScRgbUnitNits, peakNits);
        break;
    case QRhiSwapChain::HDR10:
        m_output = pqOutput(info.sdrWhiteLevel > 0 ? info.sdrWhiteLevel : color::kSdrReferenceWhiteNits, peakNits);
        break;
    case QRhiSwapChain::SDR:
    default:
        m_output = sdrOutput();
        break;
    }
    qCInfo(lcRender) << "output" << m_output.description << info;
}

void Renderer::setImage(std::vector<qfloat16> pixels, QSize size)
{
    m_pendingPixels = std::move(pixels);
    m_pendingSize = size;
    m_imagePending = true;
}

void Renderer::clearImage()
{
    m_pendingPixels.clear();
    m_imagePending = false;
    m_hasImage = false;
}

void Renderer::setOverlay(const QImage &overlay)
{
    m_pendingOverlay = overlay;
    m_overlayPending = true;
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
        QRhiTexture *texture = m_rhi->newTexture(QRhiTexture::RGBA16F, m_pendingSize, 1,
                                                 QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips);
        if (texture->create()) {
            const quint32 bytes = quint32(m_pendingPixels.size() * sizeof(qfloat16));
            updates->uploadTexture(texture, QRhiTextureUploadDescription(
                                                {0, 0, QRhiTextureSubresourceUploadDescription(m_pendingPixels.data(), bytes)}));
            updates->generateMips(texture);
            m_imageTexture->deleteLater();
            m_imageTexture = texture;
            for (QRhiShaderResourceBindings *srb : {m_imageBindingsLinear, m_imageBindingsNearest}) {
                srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
                                      0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                      m_imageUniforms),
                                  QRhiShaderResourceBinding::sampledTexture(
                                      1, QRhiShaderResourceBinding::FragmentStage, m_imageTexture,
                                      srb == m_imageBindingsLinear ? m_linearSampler : m_nearestSampler)});
                srb->create();
            }
            m_hasImage = true;
        } else {
            delete texture;
            qCWarning(lcRender) << "cannot create texture of size" << m_pendingSize;
        }
        m_pendingPixels = {};
    }

    if (m_overlayPending) {
        m_overlayPending = false;
        m_hasOverlay = !m_pendingOverlay.isNull();
        if (m_hasOverlay) {
            if (m_overlayTexture->pixelSize() != m_pendingOverlay.size()) {
                m_overlayTexture->deleteLater();
                m_overlayTexture = m_rhi->newTexture(QRhiTexture::RGBA8, m_pendingOverlay.size());
                m_overlayTexture->create();
                m_overlayBindings->setBindings(
                    {QRhiShaderResourceBinding::uniformBuffer(
                         0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                         m_overlayUniforms),
                     QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                               m_overlayTexture, m_overlaySampler)});
                m_overlayBindings->create();
            }
            updates->uploadTexture(m_overlayTexture, m_pendingOverlay);
        }
        m_pendingOverlay = QImage();
    }
    return updates;
}

void Renderer::render(const Frame &frame)
{
    if (!m_rhi || !m_swapChain || !ensureSwapChain())
        return;

    QRhi::FrameOpResult result = m_rhi->beginFrame(m_swapChain);
    if (result == QRhi::FrameOpSwapChainOutOfDate) {
        m_swapChainReady = false;
        if (!ensureSwapChain())
            return;
        result = m_rhi->beginFrame(m_swapChain);
    }
    if (result != QRhi::FrameOpSuccess) {
        qCWarning(lcRender) << "beginFrame failed" << result;
        return;
    }

    // Background: an sRGB-encoded UI grey placed at SDR white, encoded for the output.
    float clear[4] = {0, 0, 0, 1};
    for (int c = 0; c < 3; ++c) {
        const float linear = color::srgbToLinear(frame.background[c]) * m_output.scale;
        switch (m_output.mode) {
        case OutputMode::Sdr: clear[c] = frame.background[c]; break;
        case OutputMode::ScRgb: clear[c] = linear; break;
        case OutputMode::Pq: clear[c] = color::nitsToPq(linear); break;
        }
    }
    recordFrame(m_swapChain->currentFrameCommandBuffer(), m_swapChain->currentFrameRenderTarget(), m_pipeline,
                takeUpdates(), frame, m_output, clear, nullptr);
    m_rhi->endFrame(m_swapChain);
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
    float vertices[8][4];
    vertex(vertices[0], 0); // TL
    vertex(vertices[1], 3); // BL
    vertex(vertices[2], 1); // TR
    vertex(vertices[3], 2); // BR
    const QRectF o = frame.overlayRect;
    const float overlay[4][4] = {{float(o.left()), float(o.top()), 0, 0},
                                 {float(o.left()), float(o.bottom()), 0, 1},
                                 {float(o.right()), float(o.top()), 1, 0},
                                 {float(o.right()), float(o.bottom()), 1, 1}};
    std::memcpy(vertices[4], overlay, sizeof overlay);
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
    if (m_hasOverlay && !o.isEmpty()) {
        cb->setShaderResources(m_overlayBindings);
        const QRhiCommandBuffer::VertexInput input(m_vertices, kQuadBytes);
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
