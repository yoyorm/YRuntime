add_executable(YrTests_object
    test_object_db.cpp)

target_link_libraries(YrTests_object PRIVATE
    yr::object
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_object)
