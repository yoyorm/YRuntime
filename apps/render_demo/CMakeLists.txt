add_executable(yr_render_demo
    src/main.cpp)

# app 可以 include 任意 yr/ 层，但图形 API 头只留在 engine/render/vulkan/（tools/check_deps.py）。
target_link_libraries(yr_render_demo PRIVATE
    yr::render_vulkan
    yr::engine
    yr::math
    yr::core)