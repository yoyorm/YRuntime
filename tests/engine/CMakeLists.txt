add_executable(YrTests_engine
    test_engine.cpp
    test_main_loop.cpp
    test_render_extract.cpp
    test_frame_stats.cpp)

target_link_libraries(YrTests_engine PRIVATE
    yr::engine
    yr::render_null
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_engine)