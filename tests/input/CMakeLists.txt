add_executable(YrTests_input
    test_input_router.cpp)

target_link_libraries(YrTests_input PRIVATE
    yr::input
    yr_test_support
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_input)
