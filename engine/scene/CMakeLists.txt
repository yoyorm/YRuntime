add_library(yr_scene
    src/node.cpp
    src/scene_tree.cpp
    src/scene_classes.cpp
    src/packed_scene.cpp
    src/scene_reader.cpp
    src/scene_writer.cpp
    src/scene_reference.cpp)
add_library(yr::scene ALIAS yr_scene)

target_include_directories(yr_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# scene 头文件直接使用 yr/math（Vec3 / Mat4）；yr::object 也传递 math，这里显式声明意图。
# .scn 文本值编解码下沉到 yr_serialize，scene 只负责结构与诊断。
target_link_libraries(yr_scene PUBLIC yr::object yr::math yr::serialize)