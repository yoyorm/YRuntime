add_executable(YrTests_core
    test_smoke.cpp
    test_assert.cpp
    test_log.cpp
    test_handle.cpp
    test_slotmap.cpp)

target_link_libraries(YrTests_core PRIVATE
    yr::core
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_core)
