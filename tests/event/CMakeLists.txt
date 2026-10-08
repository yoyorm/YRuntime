add_executable(YrTests_event
    test_event_bus.cpp
    test_event_bus_reentrancy.cpp)

target_link_libraries(YrTests_event PRIVATE
    yr::event
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_event)