# M4 的 10000 帧集成测试。ASan/UBSan 插桩下每帧开销大，帧数降到 1000；测试语义不变，
# 只是缩小规模（用编译期宏注入，避免运行期分支）。
if(YR_ENABLE_SANITIZERS)
    set(YR_INTEGRATION_FRAMES 1000)
else()
    set(YR_INTEGRATION_FRAMES 10000)
endif()

add_executable(YrTests_integration
    test_tick_integration.cpp)

target_link_libraries(YrTests_integration PRIVATE
    yr::engine
    yr::scene
    yr::event
    yr::core
    Catch2::Catch2WithMain)

target_compile_definitions(YrTests_integration PRIVATE YR_INTEGRATION_FRAMES=${YR_INTEGRATION_FRAMES})

catch_discover_tests(YrTests_integration)