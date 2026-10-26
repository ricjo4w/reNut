#include <spirv_msl.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot read SPIR-V");
        const auto size = input.tellg();
        if (size <= 0 || size % 4 != 0) throw std::runtime_error("Invalid SPIR-V size");
        std::vector<uint32_t> words(size_t(size) / 4);
        input.seekg(0);
        input.read(reinterpret_cast<char *>(words.data()), size);
        spirv_cross::CompilerMSL msl(std::move(words));
        auto options = msl.get_msl_options();
        options.msl_version = options.make_msl_version(3, 1);
        options.argument_buffers = true;
        options.argument_buffers_tier = spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
        options.texture_buffer_native = true;
        options.enable_decoration_binding = true;
        msl.set_msl_options(options);
        auto common = msl.get_common_options();
        common.vertex.flip_vert_y = true;
        msl.set_common_options(common);
        const auto stage = msl.get_execution_model();
        msl.rename_entry_point("main", "renut_main", stage);
        spirv_cross::MSLResourceBinding push;
        push.stage = stage;
        push.desc_set = spirv_cross::kPushConstDescSet;
        push.binding = spirv_cross::kPushConstBinding;
        push.msl_buffer = 8;
        msl.add_msl_resource_binding(push);
        auto resources = msl.get_shader_resources();
        auto bind = [&](const auto &list, bool sampler) {
            for (const auto &r : list) {
                spirv_cross::MSLResourceBinding b;
                b.stage = stage;
                b.desc_set = msl.get_decoration(r.id, spv::DecorationDescriptorSet);
                b.binding = msl.get_decoration(r.id, spv::DecorationBinding);
                b.msl_texture = b.binding;
                b.msl_sampler = b.binding;
                const auto &type = msl.get_type(r.type_id);
                if (!type.array.empty() && type.array.back() == 0)
                    b.count = sampler ? 256 : 8192;
                msl.add_msl_resource_binding(b);
            }
        };
        bind(resources.separate_images, false);
        bind(resources.separate_samplers, true);
        std::ofstream output(argv[2]);
        output << msl.compile();
        if (!output) throw std::runtime_error("Cannot write Metal source");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
