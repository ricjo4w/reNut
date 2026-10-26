find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(FetchContent)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(renut_spirv_cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG vulkan-sdk-1.4.335.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(renut_spirv_cross)
add_executable(renut_spirv_to_metal "${CMAKE_SOURCE_DIR}/tools/spirv_to_metal.cpp")
target_link_libraries(renut_spirv_to_metal PRIVATE spirv-cross-msl spirv-cross-glsl spirv-cross-core)
