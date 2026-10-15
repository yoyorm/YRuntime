add_executable(YrTests_scene
    test_node.cpp
    test_scene_tree.cpp
    test_lifecycle.cpp
    test_ready.cpp
    test_propagation_guard.cpp
    test_queue_free.cpp
    test_process.cpp)

target_link_libraries(YrTests_scene PRIVATE
    yr::scene
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_scene)