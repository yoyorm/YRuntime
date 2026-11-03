add_executable(YrTests_asset
    test_asset_uid.cpp
    test_resource.cpp)

target_link_libraries(YrTests_asset PRIVATE
    yr::asset
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_asset)
