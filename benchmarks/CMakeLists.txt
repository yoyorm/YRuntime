add_executable(YrBench_slot_map
    bench_slot_map.cpp)

target_link_libraries(YrBench_slot_map PRIVATE yr::core)

# Benchmark 是手动运行的开发工具，不是 apps/ 下的 Runtime 产品。
set_target_properties(YrBench_slot_map PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks")
