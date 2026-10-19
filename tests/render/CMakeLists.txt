add_executable(YrTests_render
    test_asset_handle.cpp
    test_snapshot.cpp
    test_snapshot_io.cpp
    test_null_backend.cpp)

target_link_libraries(YrTests_render PRIVATE
    yr::render_null
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_render)