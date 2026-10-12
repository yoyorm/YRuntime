add_library(yr_scene
    src/node.cpp)
add_library(yr::scene ALIAS yr_scene)

target_include_directories(yr_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_scene PUBLIC yr::object)