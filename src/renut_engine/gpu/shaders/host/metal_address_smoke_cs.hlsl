struct Push { uint64_t source; uint64_t destination; uint count; };
[[vk::push_constant]] ConstantBuffer<Push> g_Push;
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x < g_Push.count)
        vk::RawBufferStore<uint>(g_Push.destination + id.x * 4,
            vk::RawBufferLoad<uint>(g_Push.source + id.x * 4, 4) * 3 + 7, 4);
}
