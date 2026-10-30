// Renderer, part 2: rendering into a texture and reading it back, for the output harness and
// --panel-check (D-16; D-08: split from renderer.cpp).
#include "renderer.h"

#include <rhi/qrhi.h>

#include <memory>

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
