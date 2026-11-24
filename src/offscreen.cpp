// Renderer, part 2: what a frame draws (recordFrame(), on the screen and into a texture: its quads
// and its uniform block, the color picker's included, D-115) and rendering into a texture and
// reading it back, for the output harness and --panel-check (D-16; D-08: split from renderer.cpp).
#include "renderer.h"

#include <rhi/qrhi.h>

#include <QMatrix4x4>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

// std140 layout shared with src/shaders/image.{vert,frag}.
struct Uniforms {
    float clipCorrection[16];
    float adjust[4];
    float tone[4];
    float background[4];
    float checker[4];       // x, y: cells across the texture; z: 1 = on
    float checkerColour[4]; // linear, output units
    qint32 modes[4];        // w: 1 = framebuffer rows count from the bottom
    float outside[4];       // linear, output units: where there is no image
    float imageRect[4];     // device pixels; width 0: no image
    float uvMap[4];         // texture coordinates at imageRect's top-left, their change per pixel along x
    float uvMapY[4];        // their change per pixel along y; z: target height
    float layers[Renderer::OverlayLayerCount][4]; // overlay rectangles; width 0: absent
    float detailRect[4];    // the zoom's detail, texture coordinates; width 0: none
    // The color picker (D-115), see image.frag.
    float loupeRect[4];
    float loupeStep[4];
    float loupeCentre[4];
    float currentRect[4];
    float pickedRect[4];
    float pickedColour[4];
};

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

bool Renderer::renderToBuffer(const Frame &frame, const Output &output, QSize size, std::vector<float> *rgba,
                              QString *error)
{
    if (!m_rhi) {
        *error = QStringLiteral("renderer not initialised");
        return false;
    }
    // Float32 keeps the readback free of quantisation (nothing blends); half float is the fallback.
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
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(createPipeline(renderPass.get()));
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
    recordFrame(cb, target.get(), pipeline.get(), takeUpdates(), frame, output, transparent, transparent, after);
    m_rhi->endOffscreenFrame(); // waits for the GPU; the readback is complete afterwards
    m_pendingPixels = {};
    m_pendingDetail = {};
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

QPointF Renderer::textureCoordinates(const Frame &frame, const QPointF &position)
{
    // The image quad's corners as recordFrame() gives them their coordinates, clockwise from the top left.
    QPointF uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    if (frame.mirrored)
        for (QPointF &uv : uvs)
            uv.setX(1.0 - uv.x());
    const int turns = ((frame.quarterTurns % 4) + 4) % 4;
    const auto at = [&](int screenCorner) { return uvs[(screenCorner - turns + 4) % 4]; };
    const QRectF r = frame.imageRect;
    if (r.isEmpty())
        return {-1, -1};
    const double fx = (position.x() - r.left()) / r.width(), fy = (position.y() - r.top()) / r.height();
    return at(0) + fx * (at(1) - at(0)) + fy * (at(3) - at(0));
}

void Renderer::recordFrame(QRhiCommandBuffer *cb, QRhiRenderTarget *target, QRhiGraphicsPipeline *pipeline,
                           QRhiResourceUpdateBatch *updates, const Frame &frame, const Output &output,
                           const float clearColour[4], const float outside[3], QRhiResourceUpdateBatch *afterPass)
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
    // Overlay quads: their texture coordinates carry -1 - layer (image.frag finds its pixels
    // from the screen position).
    for (int layer = 0; layer < OverlayLayerCount; ++layer) {
        const QRectF o = frame.overlayRects[layer];
        const float tag = -1.0f - float(layer);
        const float quad[4][4] = {{float(o.left()), float(o.top()), tag, 0},
                                  {float(o.left()), float(o.bottom()), tag, 0},
                                  {float(o.right()), float(o.top()), tag, 0},
                                  {float(o.right()), float(o.bottom()), tag, 0}};
        std::memcpy(vertices[4 * (1 + layer)], quad, sizeof quad);
    }
    updates->updateDynamicBuffer(m_vertices, 0, sizeof vertices, vertices);

    static_assert(sizeof(Uniforms) == kUniformBytes, "uniform block must match the shaders"); // eleven layers, the picker (D-115, D-116)
    Uniforms u{};
    std::memcpy(u.clipCorrection, projection.constData(), sizeof u.clipCorrection);
    setStage(&u, stageFor(output, frame));
    if (frame.checkerboard && frame.checkerCells[0] > 0.0f && frame.checkerCells[1] > 0.0f) {
        u.checker[0] = frame.checkerCells[0];
        u.checker[1] = frame.checkerCells[1];
        u.checker[2] = 1.0f;
    }
    for (int c = 0; c < 3; ++c) // like the background: UI colour at SDR white; the picker's checks use it too
        u.checkerColour[c] = color::srgbToLinear(frame.checkerColour[c]) * output.scale;
    u.modes[3] = m_rhi->isYUpInFramebuffer() ? 1 : 0;
    std::copy(outside, outside + 3, u.outside);
    // The image quad's texture coordinates as a function of the screen position, for the
    // overlay quads that work out the image underneath.
    if (m_hasImage && !r.isEmpty()) {
        const float topLeft[2] = {vertices[0][2], vertices[0][3]}, topRight[2] = {vertices[2][2], vertices[2][3]},
                    bottomLeft[2] = {vertices[1][2], vertices[1][3]};
        const float rect[4] = {float(r.left()), float(r.top()), float(r.width()), float(r.height())};
        std::copy(rect, rect + 4, u.imageRect);
        u.uvMap[0] = topLeft[0];
        u.uvMap[1] = topLeft[1];
        u.uvMap[2] = (topRight[0] - topLeft[0]) / rect[2];
        u.uvMap[3] = (topRight[1] - topLeft[1]) / rect[2];
        u.uvMapY[0] = (bottomLeft[0] - topLeft[0]) / rect[3];
        u.uvMapY[1] = (bottomLeft[1] - topLeft[1]) / rect[3];
    }
    u.uvMapY[2] = float(outputSize.height());
    if (frame.detail && m_detailTexture && m_hasImage) {
        const float rect[4] = {float(m_detailRect.x()), float(m_detailRect.y()), float(m_detailRect.width()),
                               float(m_detailRect.height())};
        std::copy(rect, rect + 4, u.detailRect);
    }
    for (int layer = 0; layer < OverlayLayerCount; ++layer) {
        const QRectF o = frame.overlayRects[layer];
        if (m_overlays[layer].present && !o.isEmpty()) {
            const float rect[4] = {float(o.left()), float(o.top()), float(o.width()), float(o.height())};
            std::copy(rect, rect + 4, u.layers[layer]);
        }
    }
    // The color picker (D-115): one texel of the texture per square of the loupe, along the
    // texture's axis that runs along the screen's x, then y, as the image is turned and mirrored.
    if (!frame.loupeRect.isEmpty() && m_overlays[PickerLayer].present && !frame.overlayRects[PickerLayer].isEmpty()) {
        const auto rectOf = [](const QRectF &r, float *out) {
            const float v[4] = {float(r.left()), float(r.top()), float(r.width()), float(r.height())};
            std::copy(v, v + 4, out);
        };
        rectOf(frame.loupeRect, u.loupeRect);
        rectOf(frame.currentRect, u.currentRect);
        rectOf(frame.pickedRect, u.pickedRect);
        std::copy(frame.pickedColour, frame.pickedColour + 4, u.pickedColour);
        if (m_hasImage && m_boundImage == m_imageTexture && m_imageTexture && !r.isEmpty()) {
            const QSize size = m_imageTexture->pixelSize();
            const float w = float(size.width()), h = float(size.height());
            u.loupeStep[0] = (vertices[2][2] - vertices[0][2]) / w; // top right minus top left
            u.loupeStep[1] = (vertices[2][3] - vertices[0][3]) / h;
            u.loupeStep[2] = (vertices[1][2] - vertices[0][2]) / w; // bottom left minus top left
            u.loupeStep[3] = (vertices[1][3] - vertices[0][3]) / h;
            u.loupeCentre[0] = (float(frame.loupeTexel.x()) + 0.5f) / w;
            u.loupeCentre[1] = (float(frame.loupeTexel.y()) + 0.5f) / h;
        } else {
            u.loupeCentre[0] = u.loupeCentre[1] = -1.0f; // no image: every square is checks
        }
        u.loupeCentre[2] = float(frame.loupeCells);
        u.loupeCentre[3] = std::max(1.0f, frame.pickerCheck);
    }
    updates->updateDynamicBuffer(m_imageUniforms, 0, sizeof u, &u);

    cb->beginPass(target, QColor::fromRgbF(clearColour[0], clearColour[1], clearColour[2], clearColour[3]),
                  {1.0f, 0}, updates);
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    cb->setShaderResources(frame.nearest ? m_imageBindingsNearest : m_imageBindingsLinear); // every quad's
    if (m_hasImage && !r.isEmpty()) {
        const QRhiCommandBuffer::VertexInput input(m_vertices, 0);
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }
    for (int layer = 0; layer < OverlayLayerCount; ++layer) {
        if (!m_overlays[layer].present || frame.overlayRects[layer].isEmpty())
            continue;
        const QRhiCommandBuffer::VertexInput input(m_vertices, quint32((1 + layer) * kQuadBytes));
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }
    cb->endPass(afterPass);
}
