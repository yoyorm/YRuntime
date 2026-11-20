add_executable(YrTests_math
    test_mat4.cpp
    test_math_types.cpp)

target_link_libraries(YrTests_math PRIVATE
    yr::math
    yr_test_support
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_math)
