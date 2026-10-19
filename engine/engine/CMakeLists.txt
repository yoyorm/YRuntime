add_library(yr_engine
    src/engine.cpp
    src/main_loop.cpp
    src/render_extract.cpp
    src/frame_stats.cpp)
add_library(yr::engine ALIAS yr_engine)

target_include_directories(yr_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# engine 是顶层装配层：MainLoop 只以非拥有引用持有 SceneTree / EventBus；
# render_iface 是阶段 10 抽取产出的快照契约（engine 允许依赖 render_iface，见 tools/check_deps.py）。
target_link_libraries(yr_engine PUBLIC yr::scene yr::event yr::core yr::render_iface)