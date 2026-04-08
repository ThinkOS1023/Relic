cmake_minimum_required(VERSION 3.14)
project(TsEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Sources ---
file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

find_path(CAPSTONE_INCLUDE capstone/capstone.h)
find_path(KEYSTONE_INCLUDE keystone/keystone.h)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CAPSTONE_INCLUDE}
    ${KEYSTONE_INCLUDE}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -O2
    -ffunction-sections -fdata-sections
)

find_library(CAPSTONE_LIB capstone)
find_library(KEYSTONE_LIB keystone)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CAPSTONE_LIB}
    ${KEYSTONE_LIB}
)

target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections
)
