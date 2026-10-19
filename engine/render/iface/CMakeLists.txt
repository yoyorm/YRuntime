add_library(yr_render_iface
    src/snapshot_io.cpp)
add_library(yr::render_iface ALIAS yr_render_iface)

target_include_directories(yr_render_iface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_render_iface PUBLIC yr::core)