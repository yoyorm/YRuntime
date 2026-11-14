add_executable(YrTests_job
    test_mt_queue.cpp
    test_job_system.cpp)

target_link_libraries(YrTests_job PRIVATE
    yr::job
    Catch2::Catch2WithMain)

# close() 丢唤醒在测试里表现为挂住而不是断言失败（见 test_mt_queue.cpp 的
# "close wakes every waiting consumer"）。ctest 按用例起独立进程，
# 超时只影响那一条，并把它转成可读的失败。
catch_discover_tests(YrTests_job
    PROPERTIES TIMEOUT 30)