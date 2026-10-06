#include "renderer.h"

#include "color.h"

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
    qint32 modes[4];
};
static_assert(sizeof(Uniforms) == 96, "uniform block must match the shaders");

constexpr int kVertexStride = 4 * sizeof(float); // x, y, u, v
constexpr int kQuadBytes = 4 * kVertexStride;

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

} // namespace

Renderer::Renderer(QWindow *window) : m_window(window) {}

Renderer::~Renderer()
{
    destroySwapChainResources();
    for (QRhiResource *r : std::initializer_list<QRhiResource *>{
             m_imageBindingsLinear, m_imageBindingsNearest, m_overlayBindings, m_imageTexture, m_overlayTexture,
             m_linearSampler, m_nearestSampler, m_overlaySampler, m_vertices, m_imageUniforms, m_overlayUniforms})
        delete r;
    delete m_swapChain;
    delete m_rhi;
}

bool Renderer::initialize(QString *error)
{
    switch (m_window->surfaceType()) {
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
        params.inst = m_window->vulkanInstance();
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
    if (!m_rhi) {
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

    if (!createSwapChainResources()) {
        *error = QStringLiteral("Falha ao criar a swapchain.");
        return false;
    }
    return true;
}

QString Renderer::backendName() const
{
    return m_rhi ? QString::fromLatin1(m_rhi->backendName()) : QString();
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
    return createPipeline();
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

bool Renderer::createPipeline()
{
    const QShader vs = loadShader(QStringLiteral(":/shaders/image.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/image.frag.qsb"));
    if (!vs.isValid() || !fs.isValid())
        return false;

    m_pipeline = m_rhi->newGraphicsPipeline();
    QRhiGraphicsPipeline::TargetBlend blend; // premultiplied alpha "over"
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_pipeline->setTargetBlends({blend});
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    QRhiVertexInputLayout layout;
    layout.setBindings({{kVertexStride}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                          {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_imageBindingsLinear);
    m_pipeline->setRenderPassDescriptor(m_renderPass);
    return m_pipeline->create();
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
    Output out;
    switch (m_swapChain->format()) {
    case QRhiSwapChain::HDRExtendedSrgbLinear:
    case QRhiSwapChain::HDRExtendedDisplayP3Linear:
        out.mode = OutputMode::ScRgb;
        if (info.limitsType == QRhiSwapChainHdrInfo::ColorComponentValue) {
            // macOS EDR: 1.0 is SDR white, headroom above it is available for HDR.
            out.scale = 1.0f;
            out.peak = std::max(1.0f, info.limits.colorComponentValue.maxColorComponentValue);
            out.description = QStringLiteral("EDR · headroom %1×").arg(double(out.peak), 0, 'f', 2);
        } else {
            // Windows scRGB: 1.0 = 80 cd/m2. SDR white follows the user's SDR brightness setting.
            const float white = info.sdrWhiteLevel > 0 ? info.sdrWhiteLevel : 80.0f;
            const float peak = info.limits.luminanceInNits.maxLuminance > 0 ? info.limits.luminanceInNits.maxLuminance
                                                                              : 1000.0f;
            out.scale = white / 80.0f;
            out.peak = std::max(out.scale, peak / 80.0f);
            out.description = QStringLiteral("scRGB · branco SDR %1 nits · pico %2 nits").arg(white).arg(peak);
        }
        break;
    case QRhiSwapChain::HDR10: {
        const float white = info.sdrWhiteLevel > 0 ? info.sdrWhiteLevel : color::kSdrReferenceWhiteNits;
        const float peak = info.limits.luminanceInNits.maxLuminance > 0 ? info.limits.luminanceInNits.maxLuminance
                                                                          : 1000.0f;
        out.mode = OutputMode::Pq;
        out.scale = white; // the shader works in cd/m2 for PQ
        out.peak = std::max(white, peak);
        out.description = QStringLiteral("HDR10 (PQ) · branco SDR %1 nits · pico %2 nits").arg(white).arg(peak);
        break;
    }
    case QRhiSwapChain::SDR:
    default:
        out.description = QStringLiteral("SDR (sRGB)");
        break;
    }
    m_output = out;
    qCInfo(lcRender) << "output" << out.description << info;
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

    const QSize outputSize = m_swapChain->currentPixelSize();
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
    u.adjust[0] = frame.exposure;
    u.adjust[1] = m_output.scale;
    u.adjust[2] = m_output.peak;
    u.modes[0] = int(m_output.mode);
    u.modes[1] = 0;
    u.modes[2] = frame.clipWarning ? 1 : 0;
    updates->updateDynamicBuffer(m_imageUniforms, 0, sizeof u, &u);
    u.adjust[0] = 1.0f;
    u.modes[1] = 1;
    u.modes[2] = 0;
    updates->updateDynamicBuffer(m_overlayUniforms, 0, sizeof u, &u);

    // Background: an sRGB-encoded UI grey placed at SDR white, encoded for the output.
    float bg[3];
    for (int c = 0; c < 3; ++c) {
        const float linear = color::srgbToLinear(frame.background[c]) * m_output.scale;
        switch (m_output.mode) {
        case OutputMode::Sdr: bg[c] = frame.background[c]; break;
        case OutputMode::ScRgb: bg[c] = linear; break;
        case OutputMode::Pq: bg[c] = color::nitsToPq(linear); break;
        }
    }

    QRhiCommandBuffer *cb = m_swapChain->currentFrameCommandBuffer();
    cb->beginPass(m_swapChain->currentFrameRenderTarget(), QColor::fromRgbF(bg[0], bg[1], bg[2], 1.0f),
                  {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline);
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
    cb->endPass();
    m_rhi->endFrame(m_swapChain);
}
