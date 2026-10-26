// Included after RecompiledShader. Cache keys cover this tool's binary and shader_common.h.
#pragma once
#include <cstring>
#include <fstream>
#include <dlfcn.h>
#include <mach-o/dyld.h>

static uint64_t macCompilerFingerprint(std::string_view include) {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (!size || _NSGetExecutablePath(path.data(), &size)) throw std::runtime_error("Cannot identify the shader compiler");
    // Hash the actual tool and loaded DXC library, not argv[0] or their timestamps.
    std::ifstream tool(path.data(), std::ios::binary);
    std::vector<char> executable((std::istreambuf_iterator<char>(tool)), {});
    Dl_info info{};
    if (!dladdr(reinterpret_cast<const void *>(&DxcCreateInstance), &info) || !info.dli_fname)
        throw std::runtime_error("Cannot identify the loaded DXC library");
    std::ifstream library(info.dli_fname, std::ios::binary);
    std::vector<char> dxc((std::istreambuf_iterator<char>(library)), {});
    if (executable.empty() || dxc.empty()) throw std::runtime_error("Cannot fingerprint shader compiler binaries");
    return XXH3_64bits(executable.data(), executable.size()) ^ XXH3_64bits(dxc.data(), dxc.size()) ^
        XXH3_64bits(include.data(), include.size());
}

struct MacShaderCacheHeader {
    uint32_t magic = 0x524E4D43, version = 1;
    uint32_t spec = 0, vertex = 0, texture = 0;
    uint32_t memexport[8]{}, constants[8]{};
    uint32_t inputs = 0, spirv = 0, metal[3]{};
};

static std::vector<uint8_t> readCacheBytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size)) return {};
    return bytes;
}

static bool loadMacShader(const std::filesystem::path &path, RecompiledShader &shader) {
    const auto bytes = readCacheBytes(path);
    MacShaderCacheHeader h;
    if (bytes.size() < sizeof(h)) return false;
    std::memcpy(&h, bytes.data(), sizeof(h));
    if (h.magic != 0x524E4D43 || h.version != 1 || h.inputs > 256) return false;
    const uint64_t expected = sizeof(h) + uint64_t(h.inputs) * sizeof(ShaderRecompiler::VertexInput) +
        h.spirv + uint64_t(h.metal[0]) + h.metal[1] + h.metal[2];
    if (expected != bytes.size() || !h.spirv || !h.metal[0]) return false;
    const bool pixel = (reinterpret_cast<const ShaderContainer *>(shader.data)->flags & 1) == 0;
    if (!pixel && (!h.metal[1] || !h.metal[2])) return false;
    shader.specConstantsMask = h.spec; shader.vertexFetchMemoryMask = h.vertex; shader.textureFetchMask = h.texture;
    std::copy(std::begin(h.memexport), std::end(h.memexport), std::begin(shader.memExportConstants));
    std::copy(std::begin(h.constants), std::end(h.constants), std::begin(shader.floatConstants));
    size_t offset = sizeof(h);
    shader.vertexInputs.resize(h.inputs);
    std::memcpy(shader.vertexInputs.data(), bytes.data() + offset, h.inputs * sizeof(ShaderRecompiler::VertexInput));
    offset += h.inputs * sizeof(ShaderRecompiler::VertexInput);
    shader.spirv.assign(bytes.begin() + offset, bytes.begin() + offset + h.spirv); offset += h.spirv;
    for (unsigned i = 0; i < 3; ++i) {
        shader.metalSpirv[i].assign(bytes.begin() + offset, bytes.begin() + offset + h.metal[i]);
        offset += h.metal[i];
    }
    return true;
}

static void saveMacShader(const std::filesystem::path &path, const RecompiledShader &shader) {
    MacShaderCacheHeader h;
    h.spec = shader.specConstantsMask; h.vertex = shader.vertexFetchMemoryMask; h.texture = shader.textureFetchMask;
    std::copy(std::begin(shader.memExportConstants), std::end(shader.memExportConstants), std::begin(h.memexport));
    std::copy(std::begin(shader.floatConstants), std::end(shader.floatConstants), std::begin(h.constants));
    h.inputs = uint32_t(shader.vertexInputs.size()); h.spirv = uint32_t(shader.spirv.size());
    for (unsigned i = 0; i < 3; ++i) h.metal[i] = uint32_t(shader.metalSpirv[i].size());
    const auto temporary = path.string() + ".tmp";
    std::ofstream file(temporary, std::ios::binary);
    if (!file) return;
    file.write(reinterpret_cast<const char *>(&h), sizeof(h));
    file.write(reinterpret_cast<const char *>(shader.vertexInputs.data()), h.inputs * sizeof(ShaderRecompiler::VertexInput));
    file.write(reinterpret_cast<const char *>(shader.spirv.data()), h.spirv);
    for (const auto &metal : shader.metalSpirv)
        file.write(reinterpret_cast<const char *>(metal.data()), metal.size());
    file.close();
    if (!file) return;
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}

static void writeMetalSpirv(const std::filesystem::path &dir, uint64_t hash, const RecompiledShader &shader) {
    for (unsigned i = 0; i < 3; ++i) {
        const auto &bytes = shader.metalSpirv[i];
        if (bytes.empty()) continue;
        const auto path = dir / fmt::format("{:016x}_{}.spv", hash, i);
        if (readCacheBytes(path) != bytes) writeAllBytes(path.string().c_str(), bytes.data(), bytes.size());
    }
}

// Explicit developer-only cache migration. Never enabled by normal builds:
// the caller must independently verify these existing SPIR-V files against a
// fresh build (the development migration compares the complete cache outputs).
static bool seedMacShader(const std::filesystem::path &dir, uint64_t hash, bool pixel, RecompiledShader &shader) {
    for (unsigned i = 0; i < (pixel ? 1u : 3u); ++i) {
        auto bytes = readCacheBytes(dir / fmt::format("{:016x}_{}.spv", hash, i));
        uint32_t magic = 0;
        if (bytes.size() >= 20) std::memcpy(&magic, bytes.data(), 4);
        if (magic != 0x07230203 || bytes.size() % 4) return false;
        shader.metalSpirv[i] = std::move(bytes);
    }
    const auto &bytes = shader.metalSpirv[0];
    return smolv::Encode(bytes.data(), bytes.size(), shader.spirv, smolv::kEncodeFlagStripDebugInfo);
}
