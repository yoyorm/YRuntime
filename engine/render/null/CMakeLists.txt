add_library(yr_render_null
    src/null_backend.cpp)
add_library(yr::render_null ALIAS yr_render_null)

target_include_directories(yr_render_null PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_render_null PUBLIC yr::render_iface yr::core)