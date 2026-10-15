# Vec3 / Mat4 是纯值类型，全部实现都在头文件里，没有 .cpp，
# 因此按 yr_build_flags 的 INTERFACE 模式建库（无需占位 translation unit）。
add_library(yr_math INTERFACE)
add_library(yr::math ALIAS yr_math)

target_include_directories(yr_math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# vec3.h 使用 <yr/core/assert.h> 的 YR_ASSERT_MSG（normalized() 的零向量断言），
# 因此消费者需要 core 的 include 路径与 reportAssert 的链接；INTERFACE 传递下去。
target_link_libraries(yr_math INTERFACE yr_build_flags yr::core)