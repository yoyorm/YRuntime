add_library(yr_object
    src/object.cpp
    src/object_db.cpp
    src/variant.cpp
    src/class_info.cpp
    src/class_db.cpp
    src/register_core_classes.cpp
    src/ref_counted.cpp)
add_library(yr::object ALIAS yr_object)

target_include_directories(yr_object PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# PUBLIC：variant.h 公开使用 yr::math::Vec3，消费者需要 math 的头文件路径。
target_link_libraries(yr_object PUBLIC yr::core yr::math)


