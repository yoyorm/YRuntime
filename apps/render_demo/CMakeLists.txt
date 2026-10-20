add_executable(yr_render_demo
    src/main.cpp)

# app 只 include <yr/render/...>，图形 API 头一律留在 engine/render/vulkan/（tools/check_deps.py）。
target_link_libraries(yr_render_demo PRIVATE
    yr::render_vulkan
    yr::core)