add_executable(YrTests_asset
    test_asset_uid.cpp
    test_resource.cpp
    test_asset_database.cpp
    test_gltf_loader.cpp
    test_file_access.cpp)

target_link_libraries(YrTests_asset PRIVATE
    yr::asset
    Catch2::Catch2WithMain)

target_compile_definitions(YrTests_asset PRIVATE
    YR_TEST_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
    YR_TEST_FIXTURE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

catch_discover_tests(YrTests_asset)
