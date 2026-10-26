#include "renut_engine/gpu/backend/backend.h"
#include <plume_metal.h>
#include <atomic>
#include <rex/cvar.h>
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/video.h"

namespace plume { std::unique_ptr<RenderInterface> CreateMetalInterface(); }
REXCVAR_DEFINE_STRING(renut_gpu_backend, "metal", "Nuts&Bolts/Graphics", "Native Metal renderer.")
    .allowed({"metal"}).lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace renut::gpu {
Backend ActiveBackend() { return Backend::Metal; }
const char *BackendName(Backend) { return "Metal"; }
plume::RenderShaderFormat HostShaderFormat() { return plume::RenderShaderFormat::METAL; }
std::unique_ptr<plume::RenderInterface> CreateRenderInterface() { return plume::CreateMetalInterface(); }
plume::RenderBufferFlags AddressableBufferFlags() { return plume::RenderBufferFlag::DEVICE_ADDRESSABLE | plume::RenderBufferFlag::CONSTANT; }
plume::RenderBufferFlags StorageBufferFlags() { return plume::RenderBufferFlag::UNORDERED_ACCESS | plume::RenderBufferFlag::STORAGE; }
bool TextureCreated(const plume::RenderTexture *texture) {
    return texture && static_cast<const plume::MetalTexture *>(texture)->mtl;
}
bool GraphicsPipelineCreated(const plume::RenderPipeline *pipeline) {
    return pipeline && static_cast<const plume::MetalGraphicsPipeline *>(pipeline)->state.renderPipelineState;
}
bool ReportDeviceLost(plume::RenderDevice *) {
    static std::atomic<bool> failed{false};
    if (failed.load()) return true;
    // Metal reports faults on completed command buffers, rather than on the device.
    for (const auto &list : State().command_lists) {
        if (!list) continue;
        auto *buffer = static_cast<plume::MetalCommandList *>(list.get())->mtl;
        if (buffer && buffer->status() == MTL::CommandBufferStatusError) {
            if (!failed.exchange(true)) {
                auto *error = buffer->error();
                RGPU_ERROR("Metal command buffer failed: {}", error ? error->localizedDescription()->utf8String() : "unknown GPU error");
            }
            return true;
        }
    }
    return false;
}
GpuMemoryUsage QueryGpuMemory(plume::RenderDevice *device) {
    if (!device) return {};
    auto *metal = static_cast<plume::MetalDevice *>(device)->mtl;
    if (!metal) return {};
    return {true, metal->currentAllocatedSize(), metal->recommendedMaxWorkingSetSize(), 0, 0};
}
bool SetBlendConstant(plume::RenderCommandList *cmd, const float rgba[4]) {
    auto *metal = static_cast<plume::MetalCommandList *>(cmd);
    metal->checkActiveRenderEncoder();
    metal->checkForUpdatesInGraphicsState();
    if (!metal->activeRenderEncoder) return false;
    metal->activeRenderEncoder->setBlendColor(rgba[0], rgba[1], rgba[2], rgba[3]);
    return true;
}
void SetStencilReference(plume::RenderCommandList *cmd, uint32_t reference) {
    auto *metal = static_cast<plume::MetalCommandList *>(cmd);
    metal->checkActiveRenderEncoder();
    metal->checkForUpdatesInGraphicsState();
    if (metal->activeRenderEncoder) {
        metal->activeRenderEncoder->setStencilReferenceValue(reference);
        metal->stateCache.lastStencilReference = reference;
    }
}
std::unique_ptr<plume::RenderShader> CreateHostShader(plume::RenderDevice *device,
    const HostShaderBlob &blob, const char *name) {
    if (!device || !blob.metal || !blob.metal_size) return nullptr;
    auto shader = device->createShader(blob.metal, blob.metal_size, "renut_main", HostShaderFormat());
    if (shader && !static_cast<plume::MetalShader *>(shader.get())->library) return nullptr;
    if (shader) shader->setName(name);
    return shader;
}
}
