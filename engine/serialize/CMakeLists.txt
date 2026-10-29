add_library(yr_serialize
    src/variant_codec.cpp)
add_library(yr::serialize ALIAS yr_serialize)

target_include_directories(yr_serialize PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 只依赖反射/值类型层：VariantCodec 不感知 Scene / Render。
target_link_libraries(yr_serialize PUBLIC yr::object)