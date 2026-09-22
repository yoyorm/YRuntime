cmake_minimum_required(VERSION 3.24)

project(YRuntime VERSION 0.1.0 LANGUAGES CXX)

# 选项
option(YR_BUILD_TESTS "Build unit tests" ON)
option(YR_WARNINGS_AS_ERRORS "Treat warnings as errors (CI only)" OFF)
option(YR_ENABLE_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
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

# 子目录 
add_subdirectory(engine/core)

if(YR_BUILD_TESTS)
    enable_testing()          # 必须在 add_subdirectory(tests) 之前
    add_subdirectory(tests)
endif()


if(YR_ENABLE_SANITIZERS)
    target_compile_options(yr_build_flags INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(yr_build_flags INTERFACE
        -fsanitize=address,undefined)
    message(STATUS "YRuntime: sanitizers = address + undefined")
endif()