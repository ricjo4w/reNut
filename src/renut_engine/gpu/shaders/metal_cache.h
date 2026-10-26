#pragma once
#include <cstddef>
#include <cstdint>
namespace renut::gpu {
struct MetalCacheEntry {
    uint64_t hash;
    uint32_t variant;
    size_t offset;
    size_t size;
};
extern const MetalCacheEntry g_metal_cache[];
extern const size_t g_metal_cache_count;
extern const unsigned char *g_metal_cache_bytes;
extern const size_t g_metal_cache_compressed_size, g_metal_cache_size;
}
