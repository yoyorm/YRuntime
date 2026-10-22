# Vulkan 后端：engine/render/vulkan/ 是唯一允许 include vulkan/GLFW 的目录
# （tools/check_deps.py 的 GRAPHIC_ALLOWED_DIRS）。

# 本机 SDK 布局非标准：loader 在 lib/VulkanLoader/lib，不在 $VULKAN_SDK/lib。
# 自动探测 ~/SDK/Vulkan/*/x86_64 是为了让 configure 不依赖 shell 里 source 过
# setup-env.sh（即不依赖 VULKAN_SDK 环境变量）。
if(NOT DEFINED ENV{VULKAN_SDK})
    file(GLOB _yr_vulkan_sdk_candidates "$ENV{HOME}/SDK/Vulkan/*/x86_64")
    if(_yr_vulkan_sdk_candidates)
        list(SORT _yr_vulkan_sdk_candidates)
        list(GET _yr_vulkan_sdk_candidates -1 _yr_vulkan_sdk)
        set(ENV{VULKAN_SDK} "${_yr_vulkan_sdk}")
        message(STATUS "Vulkan SDK：VULKAN_SDK 未设置，自动探测到 ${_yr_vulkan_sdk}")
    endif()
endif()

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND AND DEFINED ENV{VULKAN_SDK})
    # 回退：CMAKE_PREFIX_PATH 的每一项都会再拼 /lib，所以补到 VulkanLoader 这一级。
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}/lib/VulkanLoader")
    find_package(Vulkan QUIET)
endif()
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "未找到 Vulkan：YR_BUILD_VULKAN_BACKEND=ON 需要 Vulkan SDK（另需 GLFW3）。\n"
        "补救任选其一：\n"
        "  1) sudo apt install vulkan-validationlayers glslc，并把 Vulkan SDK 放在 "
        "~/SDK/Vulkan/*/x86_64（CMake 会自动探测，无需 VULKAN_SDK 环境变量）；\n"
        "  2) source /home/yoyorm/SDK/Vulkan/1.4.357.1/setup-env.sh 后再重新 configure。")
endif()

find_package(glfw3 3.3 REQUIRED)

add_library(yr_render_vulkan
    src/vulkan_backend.cpp
    src/platform_window.cpp
    src/vk_debug.cpp
    src/vk_instance.cpp
    src/vk_surface.cpp
    src/vk_device_selection.cpp
    src/vk_device.cpp
    src/vk_swapchain.cpp
    src/vk_command_pool.cpp
    src/vk_frame_sync.cpp)
add_library(yr::render_vulkan ALIAS yr_render_vulkan)

target_include_directories(yr_render_vulkan
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(yr_render_vulkan
    PUBLIC yr::render_iface
    PRIVATE Vulkan::Vulkan glfw)

# validation 层只在 Debug 构建启用（见 doc/layers/render.md）。
target_compile_definitions(yr_render_vulkan PRIVATE $<$<CONFIG:Debug>:YR_ENABLE_VULKAN_VALIDATION=1>)