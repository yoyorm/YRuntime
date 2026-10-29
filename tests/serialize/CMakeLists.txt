add_executable(YrTests_serialize
    test_variant_codec.cpp)

target_link_libraries(YrTests_serialize PRIVATE
    yr::serialize
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_serialize)