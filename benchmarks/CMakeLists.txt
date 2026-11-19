add_executable(YrBench_slot_map
    bench_slot_map.cpp)

add_executable(YrBench_jobs
    bench_jobs.cpp)

add_executable(YrBench_asset_async
    bench_asset_async.cpp)

target_link_libraries(YrBench_slot_map PRIVATE yr::core)
target_link_libraries(YrBench_jobs PRIVATE yr::job)
target_link_libraries(YrBench_asset_async PRIVATE yr::asset)

# Benchmark 是手动运行的开发工具，不是 apps/ 下的 Runtime 产品。
set_target_properties(YrBench_slot_map YrBench_jobs YrBench_asset_async PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks")
