# Catch2：优先用系统包（本机已装 3.7.1，configure 只要 0.15s）；
# 找不到就用 FetchContent 拉源码（CI 的干净环境需要这条路 —— Ubuntu 24.04 的 apt
# 源里没有 libcatch2-dev，见 ADR D13）。两条路最终都提供 Catch2::Catch2WithMain。
find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    message(STATUS "YRuntime: 未找到系统 Catch2，改用 FetchContent 拉取 v3.7.1（首次会慢一些）")
    include(FetchContent)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    # SYSTEM：把 Catch2 的头文件目录标记为"系统头"，编译器不对其内容报警告。
    # 不加的话，clang + 我们的严格警告集（-Wold-style-cast 等）可能会被 Catch2 内部
    # 代码刷出几十条我们无法修复的警告。系统包路径天然是 SYSTEM，FetchContent 不是。
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    # FetchContent 方式下 Catch.cmake 不在默认搜索路径里，显式补上
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(Catch)          # 提供 catch_discover_tests()，两种方式都需要

add_subdirectory(core)
