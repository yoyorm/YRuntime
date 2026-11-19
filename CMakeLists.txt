cmake_minimum_required(VERSION 3.24)

project(YRuntime VERSION 0.1.0 LANGUAGES CXX)

# 选项
option(YR_BUILD_TESTS "Build unit tests" ON)
option(YR_BUILD_BENCHMARKS "Build manual benchmark executables" OFF)
option(YR_BUILD_VULKAN_BACKEND "Build the Vulkan backend (needs Vulkan SDK + GLFW + GPU)" OFF)
option(YR_WARNINGS_AS_ERRORS "Treat warnings as errors (CI only)" OFF)
option(YR_ENABLE_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(YR_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer (mutually exclusive with YR_ENABLE_SANITIZERS)" OFF)
option(YR_ENABLE_ASSERTS "Enable runtime assertions" ON)
option(YR_ENABLE_LOG "Enable logging" ON)
option(YR_ENABLE_DEBUG_LOG "Compile in DEBUG-level logging (stripped in release)" ON)

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # 给 clangd / IDE 用

# 公共编译设置：一个 INTERFACE target，所有 yr_* 库链接它 ----
add_library(yr_build_flags INTERFACE)
target_compile_features(yr_build_flags INTERFACE cxx_std_20)
target_compile_options(yr_build_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
    $<$<BOOL:${YR_WARNINGS_AS_ERRORS}>:-Werror>)

# 子目录（按依赖方向排列：core → math → object ...）
add_subdirectory(engine/core)
add_subdirectory(engine/math)
add_subdirectory(engine/job)
add_subdirectory(engine/object)
add_subdirectory(engine/asset)
add_subdirectory(engine/event)
add_subdirectory(engine/serialize)
add_subdirectory(engine/scene)
add_subdirectory(engine/render)
add_subdirectory(engine/engine)

# 可运行物（apps 允许依赖任何层）
add_subdirectory(apps)

if(YR_BUILD_TESTS)
    # TSan 与 ASLR 冲突：部分内核/容器下测试二进制直接启动会报
    # "unexpected memory mapping"。POST_BUILD 的用例发现（catch_discover_tests）
    # 会在构建期执行测试二进制，因而整次构建失败；改为 PRE_TEST 后，
    # 发现发生在 ctest 启动时，配合 tools/ctest_tsan.sh 的 setarch -R 即可。
    if(YR_ENABLE_THREAD_SANITIZER)
        set(CMAKE_CATCH_DISCOVER_TESTS_DISCOVERY_MODE "PRE_TEST")
        message(STATUS "YRuntime: TSan 下改用 PRE_TEST 用例发现（ctest 需经 tools/ctest_tsan.sh 运行）")
    endif()

    enable_testing()          # 必须在 add_subdirectory(tests) 之前
    add_subdirectory(tests)
endif()

if(YR_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()


if(YR_ENABLE_SANITIZERS AND YR_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "YR_ENABLE_SANITIZERS 与 YR_ENABLE_THREAD_SANITIZER 不能同时开启（ASan 与 TSan 互斥）")
endif()

if(YR_ENABLE_SANITIZERS)
    target_compile_options(yr_build_flags INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(yr_build_flags INTERFACE
        -fsanitize=address,undefined)
    message(STATUS "YRuntime: sanitizers = address + undefined")
endif()

if(YR_ENABLE_THREAD_SANITIZER)
    target_compile_options(yr_build_flags INTERFACE
        -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(yr_build_flags INTERFACE
        -fsanitize=thread)
    message(STATUS "YRuntime: sanitizer = thread")
endif()
