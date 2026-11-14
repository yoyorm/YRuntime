add_executable(YrTests_job
    test_mt_queue.cpp)

target_link_libraries(YrTests_job PRIVATE
    yr::job
    Catch2::Catch2WithMain)

catch_discover_tests(YrTests_job)
