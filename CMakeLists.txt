cmake_minimum_required(VERSION 3.14)
project(TsEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Sources ---
file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -O2
    -ffunction-sections -fdata-sections
)

target_link_libraries(${PROJECT_NAME} PRIVATE capstone)

target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections
)
