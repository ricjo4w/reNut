#pragma once

#include <cstdlib>
#include <filesystem>
#include <mach-o/dyld.h>
#include <vector>

namespace renut {
// Called before the SDK initializes SDL. Its static SDL and ours then share the
// newer runtime through SDL's supported dynamic API dispatch mechanism.
inline void ConfigureMacSDL() {
    if (std::getenv("SDL3_DYNAMIC_API")) return;
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) != 0) return;
    const auto directory = std::filesystem::weakly_canonical(path.data()).parent_path();
    for (const auto& library : {directory / "../Frameworks/libSDL3.0.dylib",
                                directory / "mac-sdl/lib/libSDL3.0.dylib"}) {
        if (std::filesystem::is_regular_file(library)) {
            setenv("SDL3_DYNAMIC_API", library.c_str(), 0);
            return;
        }
    }
}
}
