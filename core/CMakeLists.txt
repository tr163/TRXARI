cmake_minimum_required(VERSION 3.15)
project(guidecore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(guidecore STATIC
    src/axis_stats.cpp
    src/algorithms.cpp
    src/us_image.cpp
    src/star.cpp
    src/guide_session.cpp
    src/calibration.cpp
)
target_include_directories(guidecore PUBLIC include)

add_executable(test_algorithms tests/test_algorithms.cpp)
target_link_libraries(test_algorithms PRIVATE guidecore)

add_executable(test_star tests/test_star.cpp)
target_link_libraries(test_star PRIVATE guidecore)

add_executable(test_guide_session tests/test_guide_session.cpp)
target_link_libraries(test_guide_session PRIVATE guidecore)

add_executable(test_calibration tests/test_calibration.cpp)
target_link_libraries(test_calibration PRIVATE guidecore)

add_executable(guide_sim tools/guide_sim.cpp)
target_link_libraries(guide_sim PRIVATE guidecore)

add_executable(star_sim tools/star_sim.cpp)
target_link_libraries(star_sim PRIVATE guidecore)

add_executable(guide_replay tools/guide_replay.cpp)
target_link_libraries(guide_replay PRIVATE guidecore)

enable_testing()
add_test(NAME algorithms COMMAND test_algorithms)
add_test(NAME star COMMAND test_star)
add_test(NAME guide_session COMMAND test_guide_session)
add_test(NAME calibration COMMAND test_calibration)
