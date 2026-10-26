// ReXGlue 0.10.0 snapshots an audio callback, unlocks, then executes guest
// code. UnregisterClient can destroy/clear its driver in that interval.
// Serialize the guest-facing lifetime operations and discard late submissions
// after unregister, before they reach the SDK's unchecked driver dereference.
#include <rex/hook.h>
#include <array>
#include <mutex>

namespace rex::kernel::xboxkrnl {
u32 XAudioRegisterRenderDriverClient_entry(mapped_u32, mapped_u32);
u32 XAudioUnregisterRenderDriverClient_entry(mapped_void);
u32 XAudioSubmitRenderDriverFrame_entry(mapped_void, mapped_void);
}

namespace {
std::mutex audio_clients_mutex;
std::array<bool, 8> active_audio_clients{};
constexpr uint32_t kDriverTag = 0x41550000;
bool IsActive(uint32_t handle) {
    const auto index = handle & 0xffff;
    return (handle & 0xffff0000) == kDriverTag && index < active_audio_clients.size()
        && active_audio_clients[index];
}
}

REX_HOOK_RAW(__imp__XAudioRegisterRenderDriverClient) {
    const auto out = ctx.r4.u32;
    std::lock_guard lock(audio_clients_mutex);
    rex::ppc::HostToGuestFunction<
        &rex::kernel::xboxkrnl::XAudioRegisterRenderDriverClient_entry>(ctx, base);
    if (ctx.r3.u32 == 0 && out) {
        const auto handle = rex::memory::load_and_swap<uint32_t>(base + out);
        const auto index = handle & 0xffff;
        if ((handle & 0xffff0000) == kDriverTag && index < active_audio_clients.size())
            active_audio_clients[index] = true;
    }
}

REX_HOOK_RAW(__imp__XAudioUnregisterRenderDriverClient) {
    std::lock_guard lock(audio_clients_mutex);
    const auto handle = ctx.r3.u32;
    if (!IsActive(handle)) {
        ctx.r3.u64 = 0;
        return;
    }
    active_audio_clients[handle & 0xffff] = false;
    rex::ppc::HostToGuestFunction<
        &rex::kernel::xboxkrnl::XAudioUnregisterRenderDriverClient_entry>(ctx, base);
}

REX_HOOK_RAW(__imp__XAudioSubmitRenderDriverFrame) {
    std::lock_guard lock(audio_clients_mutex);
    if (!IsActive(ctx.r3.u32)) {
        ctx.r3.u64 = 0;
        return;
    }
    rex::ppc::HostToGuestFunction<
        &rex::kernel::xboxkrnl::XAudioSubmitRenderDriverFrame_entry>(ctx, base);
}

namespace renut::audio {
void InstallMacClientHooks() {
    // GetProcAddressByOrdinal creates new guest thunks from this registry.
    // Cover those dynamic calls as well as the generated static import calls.
    auto& registry = rex::ppc::GetPPCFuncRegistry();
    for (const auto& hook : {
            std::pair{"__imp__XAudioRegisterRenderDriverClient", &__imp__XAudioRegisterRenderDriverClient},
            std::pair{"__imp__XAudioUnregisterRenderDriverClient", &__imp__XAudioUnregisterRenderDriverClient},
            std::pair{"__imp__XAudioSubmitRenderDriverFrame", &__imp__XAudioSubmitRenderDriverFrame}}) {
        bool replaced = false;
        for (auto& entry : registry) {
            if (std::strcmp(entry.first, hook.first) == 0) {
                entry.second = hook.second;
                replaced = true;
            }
        }
        if (!replaced) registry.push_back(hook);
    }
}
}
