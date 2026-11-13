add_executable(YrTests_engine
    test_engine.cpp
    test_main_loop.cpp
    test_render_extract.cpp
    test_frame_stats.cpp)

target_link_libraries(YrTests_engine PRIVATE
    yr::engine
    yr::asset
    yr::render_null
    Catch2::Catch2WithMain)

target_compile_definitions(YrTests_engine PRIVATE
    YR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets")

catch_discover_tests(YrTests_engine)