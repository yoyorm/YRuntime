add_library(yr_job STATIC
    src/job_system.cpp)
add_library(yr::job ALIAS yr_job)

target_include_directories(yr_job PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_job PUBLIC yr_build_flags yr::core)