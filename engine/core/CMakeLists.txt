add_library(yr_core
    src/version.cpp
    src/assert.cpp
    src/log.cpp
)
add_library(yr::core ALIAS yr_core)

target_include_directories(yr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_core PUBLIC yr_build_flags)

# PUBLIC：这些宏在头文件里展开，所有消费者（含测试）必须看到同一个值。
# 生成器表达式保证 OFF 时显式下发 =0，而不是"不定义"（不定义会退回 NDEBUG 推导）。
target_compile_definitions(yr_core PUBLIC
    YR_VERSION_STRING="${PROJECT_VERSION}"
    YR_BUILD_CONFIG="$<CONFIG>"
    $<$<BOOL:${YR_ENABLE_ASSERTS}>:YR_ENABLE_ASSERTS=1>
    $<$<NOT:$<BOOL:${YR_ENABLE_ASSERTS}>>:YR_ENABLE_ASSERTS=0>
    $<$<BOOL:${YR_ENABLE_LOG}>:YR_ENABLE_LOG=1>
    $<$<NOT:$<BOOL:${YR_ENABLE_LOG}>>:YR_ENABLE_LOG=0>
    $<$<BOOL:${YR_ENABLE_DEBUG_LOG}>:YR_ENABLE_DEBUG_LOG=1>
    $<$<NOT:$<BOOL:${YR_ENABLE_DEBUG_LOG}>>:YR_ENABLE_DEBUG_LOG=0>)
