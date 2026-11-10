cmake_minimum_required(VERSION 3.15)
project(guidecore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(guidecore STATIC
    src/axis_stats.cpp
    src/algorithms.cpp
)
target_include_directories(guidecore PUBLIC include)

add_executable(test_algorithms tests/test_algorithms.cpp)
target_link_libraries(test_algorithms PRIVATE guidecore)

add_executable(guide_sim tools/guide_sim.cpp)
target_link_libraries(guide_sim PRIVATE guidecore)

enable_testing()
add_test(NAME algorithms COMMAND test_algorithms)
