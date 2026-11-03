add_library(yr_asset
    src/asset_uid.cpp
    src/resource.cpp)
add_library(yr::asset ALIAS yr_asset)

target_include_directories(yr_asset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 资源类型是数据载体：RefCounted 来自 object，MeshVertex 的 Vec3 来自 math。
target_link_libraries(yr_asset PUBLIC yr::object yr::math)
