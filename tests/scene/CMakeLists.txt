add_executable(YrTests_scene
    test_node.cpp)

target_link_libraries(YrTests_scene PRIVATE
    yr::scene
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_scene)