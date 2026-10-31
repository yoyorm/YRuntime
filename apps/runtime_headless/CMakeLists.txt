add_executable(yr_runtime_headless
    main.cpp)

# apps 允许依赖任何层（见 tools/check_deps.py）；这里显式列出直接使用的最低层。
target_link_libraries(yr_runtime_headless PRIVATE
    yr::engine
    yr::scene
    yr::object
    yr::render_null
    yr::core)