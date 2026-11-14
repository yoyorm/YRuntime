add_library(yr_job INTERFACE)
add_library(yr::job ALIAS yr_job)

target_include_directories(yr_job INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_job INTERFACE yr_build_flags)
