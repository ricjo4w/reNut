// Render through the same bindless host shaders as the game and verify GPU readback.
#include <plume_render_interface.h>
#include <plume_render_interface_builders.h>
#include <cstdio>
#include <cstring>
#include "host_shaders/present_vs.metal.h"
#include "host_shaders/present_ps.metal.h"
#include "host_shaders/metal_address_smoke_cs.metal.h"
#include "host_shaders/depth_resolve_ps.metal.h"
namespace plume { std::unique_ptr<RenderInterface> CreateMetalInterface(); }

int main() {
    using namespace plume;
    auto interface = CreateMetalInterface();
    if (!interface) return 1;
    auto device = interface->createDevice();
    if (!device) return 2;
    auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
    auto command = queue->createCommandList();
    auto fence = device->createCommandFence();
    auto upload = device->createBuffer(RenderBufferDesc::UploadBuffer(256));
    auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(1024));
    auto *data = static_cast<unsigned char *>(upload->map());
    std::memset(data, 0, 256);
    data[0] = 255; data[3] = 255;
    auto source = device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, RenderFormat::R8G8B8A8_UNORM));
    auto target = device->createTexture(RenderTextureDesc::Texture2D(4, 4, 1, RenderFormat::R8G8B8A8_UNORM, RenderTextureFlag::RENDER_TARGET));
    auto vs = device->createShader(g_present_vs_metal, sizeof(g_present_vs_metal), "renut_main", RenderShaderFormat::METAL);
    auto ps = device->createShader(g_present_ps_metal, sizeof(g_present_ps_metal), "renut_main", RenderShaderFormat::METAL);
    RenderDescriptorSetBuilder textures, samplers;
    textures.begin(); textures.addTexture(0, 8192); textures.end(true, 8192);
    samplers.begin(); samplers.addSampler(0, 256); samplers.end(true, 256);
    auto textureSet = textures.create(device.get());
    auto samplerSet = samplers.create(device.get());
    auto sampler = device->createSampler(RenderSamplerDesc{});
    textureSet->setTexture(0, source.get(), RenderTextureLayout::SHADER_READ);
    samplerSet->setSampler(0, sampler.get());
    RenderPipelineLayoutBuilder layout;
    layout.begin(false, true);
    layout.addDescriptorSet(textures); layout.addDescriptorSet(samplers);
    layout.addPushConstant(0, 2, 32, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    layout.end();
    auto pipelineLayout = layout.create(device.get());
    RenderGraphicsPipelineDesc desc;
    desc.pipelineLayout = pipelineLayout.get(); desc.vertexShader = vs.get(); desc.pixelShader = ps.get();
    desc.renderTargetCount = 1; desc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
    desc.renderTargetBlend[0] = RenderBlendDesc::Copy(); desc.cullMode = RenderCullMode::NONE;
    desc.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
    auto pipeline = device->createGraphicsPipeline(desc);
    const RenderTexture *attachment = target.get();
    RenderFramebufferDesc framebufferDesc;
    framebufferDesc.colorAttachments = &attachment; framebufferDesc.colorAttachmentsCount = 1;
    auto framebuffer = device->createFramebuffer(framebufferDesc);
    command->begin();
    command->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(source.get(), RenderTextureLayout::COPY_DEST));
    command->copyTextureRegion(RenderTextureCopyLocation::Subresource(source.get()),
        RenderTextureCopyLocation::PlacedFootprint(upload.get(), RenderFormat::R8G8B8A8_UNORM, 1, 1, 1, 64, 0));
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(source.get(), RenderTextureLayout::SHADER_READ));
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target.get(), RenderTextureLayout::COLOR_WRITE));
    command->setFramebuffer(framebuffer.get());
    command->setViewports(RenderViewport(0, 0, 4, 4)); command->setScissors(RenderRect(0, 0, 4, 4));
    command->setGraphicsPipelineLayout(pipelineLayout.get());
    command->setGraphicsDescriptorSet(textureSet.get(), 0); command->setGraphicsDescriptorSet(samplerSet.get(), 1);
    command->setPipeline(pipeline.get());
    struct Push { float scale[2], translate[2]; uint32_t texture, sampler; float p0, p1; } push{{1,1},{0,0},0,0,0,0};
    command->setGraphicsPushConstants(0, &push, 0, sizeof(push));
    command->drawInstanced(3, 1, 0, 0);
    command->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(target.get(), RenderTextureLayout::COPY_SOURCE));
    command->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(readback.get(), RenderFormat::R8G8B8A8_UNORM, 4, 4, 1, 64, 0),
        RenderTextureCopyLocation::Subresource(target.get()));
    command->end(); queue->executeCommandLists(command.get(), fence.get()); queue->waitForCommandFence(fence.get());
    const auto *pixels = static_cast<unsigned char *>(readback->map());
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        const auto *p = pixels + y * 256 + x * 4;
        if (p[0] != 255 || p[1] != 0 || p[2] != 0 || p[3] != 255) {
            std::fprintf(stderr, "Metal readback mismatch at %u,%u: %u %u %u %u\n", x,y,p[0],p[1],p[2],p[3]); return 3;
        }
    }
    auto addresses = device->createBuffer(RenderBufferDesc::UploadBuffer(256,
        RenderBufferFlag::DEVICE_ADDRESSABLE | RenderBufferFlag::UNORDERED_ACCESS | RenderBufferFlag::STORAGE));
    auto *words = static_cast<uint32_t *>(addresses->map());
    words[0] = 5; words[1] = 100; words[16] = words[17] = 0;
    auto cs = device->createShader(g_metal_address_smoke_cs_metal, sizeof(g_metal_address_smoke_cs_metal), "renut_main", RenderShaderFormat::METAL);
    RenderPipelineLayoutBuilder computeLayout;
    computeLayout.begin(); computeLayout.addPushConstant(0, 0, 24, RenderShaderStageFlag::COMPUTE); computeLayout.end();
    auto computePipelineLayout = computeLayout.create(device.get());
    RenderComputePipelineDesc computeDesc;
    computeDesc.pipelineLayout = computePipelineLayout.get(); computeDesc.computeShader = cs.get();
    computeDesc.threadGroupSizeX = 64; computeDesc.threadGroupSizeY = computeDesc.threadGroupSizeZ = 1;
    auto computePipeline = device->createComputePipeline(computeDesc);
    struct AddressPush { uint64_t source, destination; uint32_t count, pad; }
        addressPush{addresses->getDeviceAddress(), addresses->getDeviceAddress() + 64, 2, 0};
    command->begin();
    command->setComputePipelineLayout(computePipelineLayout.get()); command->setPipeline(computePipeline.get());
    command->setComputePushConstants(0, &addressPush, 0, sizeof(addressPush)); command->dispatch(1, 1, 1);
    command->end(); queue->executeCommandLists(command.get(), fence.get()); queue->waitForCommandFence(fence.get());
    if (words[16] != 22 || words[17] != 307) return 4;
    auto depth = device->createTexture(RenderTextureDesc::Texture2D(4, 4, 1, RenderFormat::D32_FLOAT_S8_UINT,
        RenderTextureFlag::DEPTH_TARGET));
    auto resolved = device->createTexture(RenderTextureDesc::Texture2D(4, 4, 1, RenderFormat::R32_FLOAT,
        RenderTextureFlag::RENDER_TARGET));
    RenderFramebufferDesc depthDesc; depthDesc.depthAttachment = depth.get();
    auto depthFramebuffer = device->createFramebuffer(depthDesc);
    RenderFramebufferDesc colorDepthDesc = framebufferDesc;
    colorDepthDesc.depthAttachment = depth.get();
    auto colorDepthFramebuffer = device->createFramebuffer(colorDepthDesc);
    auto depthView = depth->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::D32_FLOAT_S8_UINT));
    textureSet->setTexture(1, depth.get(), RenderTextureLayout::SHADER_READ, depthView.get());
    const RenderTexture *resolveAttachments[] = {resolved.get(), target.get()};
    RenderFramebufferDesc resolveDesc; resolveDesc.colorAttachments = resolveAttachments; resolveDesc.colorAttachmentsCount = 2;
    auto resolveFramebuffer = device->createFramebuffer(resolveDesc);
    auto depthPS = device->createShader(g_depth_resolve_ps_metal, sizeof(g_depth_resolve_ps_metal), "renut_main", RenderShaderFormat::METAL);
    desc.pixelShader = depthPS.get(); desc.renderTargetCount = 2;
    desc.renderTargetFormat[0] = RenderFormat::R32_FLOAT; desc.renderTargetFormat[1] = RenderFormat::R8G8B8A8_UNORM;
    desc.renderTargetBlend[1] = RenderBlendDesc::Copy();
    auto depthPipeline = device->createGraphicsPipeline(desc);
    command->begin();
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(depth.get(), RenderTextureLayout::DEPTH_WRITE));
    command->setFramebuffer(depthFramebuffer.get()); command->clearDepthStencil(true, true, 0.375f, 0);
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target.get(), RenderTextureLayout::COLOR_WRITE));
    command->setFramebuffer(colorDepthFramebuffer.get());
    const RenderRect partialClear(1, 1, 3, 3);
    command->clearColor(0, RenderColor(0, 0, 1, 1), &partialClear, 1);
    command->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(target.get(), RenderTextureLayout::COPY_SOURCE));
    command->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(readback.get(), RenderFormat::R8G8B8A8_UNORM, 4, 4, 1, 64, 0),
        RenderTextureCopyLocation::Subresource(target.get()));
    command->end(); queue->executeCommandLists(command.get(), fence.get()); queue->waitForCommandFence(fence.get());
    pixels = static_cast<unsigned char *>(readback->map());
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        const bool blue = x >= 1 && x < 3 && y >= 1 && y < 3;
        const auto *p = pixels + y * 256 + x * 4;
        if (p[0] != (blue ? 0 : 255) || p[1] != 0 || p[2] != (blue ? 255 : 0) || p[3] != 255) {
            std::fprintf(stderr, "Partial color clear mismatch at %u,%u\n", x, y); return 6;
        }
    }
    command->begin();
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(depth.get(), RenderTextureLayout::SHADER_READ));
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(resolved.get(), RenderTextureLayout::COLOR_WRITE));
    command->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target.get(), RenderTextureLayout::COLOR_WRITE));
    command->setFramebuffer(resolveFramebuffer.get());
    command->setGraphicsPipelineLayout(pipelineLayout.get());
    command->setGraphicsDescriptorSet(textureSet.get(), 0); command->setGraphicsDescriptorSet(samplerSet.get(), 1);
    command->setPipeline(depthPipeline.get());
    command->setViewports(RenderViewport(0, 0, 4, 4)); command->setScissors(RenderRect(0, 0, 4, 4));
    push.texture = 1;
    command->setGraphicsPushConstants(0, &push, 0, sizeof(push)); command->drawInstanced(3, 1, 0, 0);
    command->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(resolved.get(), RenderTextureLayout::COPY_SOURCE));
    command->copyTextureRegion(RenderTextureCopyLocation::PlacedFootprint(readback.get(), RenderFormat::R32_FLOAT, 4, 4, 1, 64),
        RenderTextureCopyLocation::Subresource(resolved.get()));
    command->end(); queue->executeCommandLists(command.get(), fence.get()); queue->waitForCommandFence(fence.get());
    const auto *depthBytes = static_cast<unsigned char *>(readback->map());
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
        float value = 0; std::memcpy(&value, depthBytes + y * 256 + x * 4, 4);
        if (value != 0.375f) { std::fprintf(stderr, "Depth resolve mismatch: %f\n", value); return 5; }
    }
    std::puts("Metal bindless textures, samplers, push constants, drawing, readback, GPU addresses, partial clears and depth preservation/resolve passed.");
}
