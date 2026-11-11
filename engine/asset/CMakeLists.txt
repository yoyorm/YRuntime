add_library(yr_asset_thirdparty STATIC
    ${PROJECT_SOURCE_DIR}/third_party/tinygltf/tinygltf_impl.cpp
    ${PROJECT_SOURCE_DIR}/third_party/stb/stb_image_impl.cpp)
target_compile_features(yr_asset_thirdparty PRIVATE cxx_std_20)
# 第三方代码不链接 yr_build_flags，避免项目警告集作用到上游实现。
target_include_directories(yr_asset_thirdparty SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/tinygltf
    ${PROJECT_SOURCE_DIR}/third_party/stb)

add_library(yr_asset
    src/asset_uid.cpp
    src/resource.cpp
    src/asset_database.cpp
    src/gltf_loader.cpp)
add_library(yr::asset ALIAS yr_asset)

target_include_directories(yr_asset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 资源类型是数据载体：RefCounted 来自 object，MeshVertex 的 Vec3 来自 math。
# glTF/图像实现是 PRIVATE：公共头不暴露第三方库。
target_link_libraries(yr_asset PUBLIC yr::object yr::math PRIVATE yr_asset_thirdparty)
target_include_directories(yr_asset SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/tinygltf
    ${PROJECT_SOURCE_DIR}/third_party/stb)
