add_executable(YrTests_gameplay
    test_gameplay_nodes.cpp)

target_link_libraries(YrTests_gameplay PRIVATE
    yr::gameplay
    yr::input
    yr::scene
    yr_test_support
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_gameplay)
