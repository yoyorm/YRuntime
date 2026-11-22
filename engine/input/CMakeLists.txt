add_library(yr_input
    src/input_router.cpp
    src/input_state.cpp
    src/input_system.cpp)
add_library(yr::input ALIAS yr_input)

target_include_directories(yr_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_input PUBLIC yr::core yr::event)
