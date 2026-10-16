add_library(yr_scene
    src/node.cpp
    src/scene_tree.cpp)
add_library(yr::scene ALIAS yr_scene)

target_include_directories(yr_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# scene 头文件直接使用 yr/math（Vec3 / Mat4）；yr::object 也传递 math，这里显式声明意图。
target_link_libraries(yr_scene PUBLIC yr::object yr::math)