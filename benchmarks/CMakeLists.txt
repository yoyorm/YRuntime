add_executable(YrBench_slot_map
    bench_slot_map.cpp)

add_executable(YrBench_jobs
    bench_jobs.cpp)

target_link_libraries(YrBench_slot_map PRIVATE yr::core)
target_link_libraries(YrBench_jobs PRIVATE yr::job)

# Benchmark 是手动运行的开发工具，不是 apps/ 下的 Runtime 产品。
set_target_properties(YrBench_slot_map YrBench_jobs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks")
