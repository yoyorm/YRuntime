add_library(yr_event
    src/event_bus.cpp
    src/subscription.cpp)
add_library(yr::event ALIAS yr_event)

target_include_directories(yr_event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(yr_event PUBLIC yr::object)