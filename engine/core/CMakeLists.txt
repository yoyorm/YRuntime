add_library(yr_core src/version.cpp)
add_library(yr::core ALIAS yr_core)

target_include_directories(yr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_core PUBLIC yr_build_flags)

target_compile_definitions(yr_core PRIVATE
    YR_VERSION_STRING="${PROJECT_VERSION}"
    YR_BUILD_CONFIG="$<CONFIG>")