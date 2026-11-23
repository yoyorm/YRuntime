add_library(yr_gameplay
    src/input_axis.cpp
    src/mouse_look.cpp
    src/sun_sweep.cpp
    src/key_trigger.cpp
    src/move_motor.cpp
    src/box_collider.cpp
    src/counter.cpp
    src/gameplay_classes.cpp)
add_library(yr::gameplay ALIAS yr_gameplay)

# gameplay 是组合层：只经 scene/input 的公开接口读写节点状态，不碰渲染或资源细节。
target_include_directories(yr_gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_gameplay PUBLIC yr::scene yr::input yr::object yr::math yr::core)
