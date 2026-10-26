# Apply reNut's Metal fixes to a build-directory copy; keep the pinned submodule intact.
find_package(Git REQUIRED)
set(renut_plume_patch "${CMAKE_SOURCE_DIR}/patches/plume-metal.patch")
set(renut_plume_patched "${CMAKE_BINARY_DIR}/plume-patched")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${renut_plume_patch}" "${CMAKE_SOURCE_DIR}/thirdparty/plume/plume_metal.cpp"
    "${CMAKE_SOURCE_DIR}/thirdparty/plume/plume_metal.h")
file(MAKE_DIRECTORY "${renut_plume_patched}")
configure_file("${CMAKE_SOURCE_DIR}/thirdparty/plume/plume_metal.cpp"
    "${renut_plume_patched}/plume_metal.cpp" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/thirdparty/plume/plume_metal.h"
    "${renut_plume_patched}/plume_metal.h" COPYONLY)
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths "--directory=${renut_plume_patched}"
    "${renut_plume_patch}" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE renut_patch_result ERROR_VARIABLE renut_patch_error)
if(renut_patch_result)
    message(FATAL_ERROR "Cannot apply the pinned Plume Metal fixes: ${renut_patch_error}")
endif()
file(READ "${renut_plume_patched}/plume_metal.cpp" renut_patched_contents)
if(NOT renut_patched_contents MATCHES "dstBuffer != nullptr && srcTexture != nullptr")
    message(FATAL_ERROR "Plume patch command skipped the Metal readback fix")
endif()
get_target_property(renut_plume_sources plume SOURCES)
list(REMOVE_ITEM renut_plume_sources plume_metal.cpp)
set_property(TARGET plume PROPERTY SOURCES "${renut_plume_sources}")
target_sources(plume PRIVATE "${renut_plume_patched}/plume_metal.cpp")
# All consumers must see the same MetalDevice layout as the implementation.
target_include_directories(plume BEFORE PUBLIC "${renut_plume_patched}")
