add_library(yr_engine
    src/engine.cpp
    src/main_loop.cpp
    src/frame_stats.cpp)
add_library(yr::engine ALIAS yr_engine)

target_include_directories(yr_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# engine 是顶层装配层：MainLoop 只以非拥有引用持有 SceneTree / EventBus。
target_link_libraries(yr_engine PUBLIC yr::scene yr::event yr::core)