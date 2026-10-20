add_subdirectory(runtime_headless)

if(YR_BUILD_VULKAN_BACKEND)
    add_subdirectory(render_demo)
endif()