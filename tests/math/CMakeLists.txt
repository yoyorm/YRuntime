add_executable(YrTests_math
    test_vec3.cpp
    test_mat4.cpp
    test_quat.cpp
    test_transform.cpp)

target_link_libraries(YrTests_math PRIVATE
    yr::math
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_math)
