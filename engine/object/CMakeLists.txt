add_library(yr_object
    src/object.cpp
    src/object_db.cpp
    src/variant.cpp
    src/class_info.cpp)
add_library(yr::object ALIAS yr_object)

target_include_directories(yr_object PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_object PUBLIC yr::core)
