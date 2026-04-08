cmake_minimum_required(VERSION 3.14)
project(TsEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ── Capstone (反汇编) ──
# 先找系统安装, 没有则自动下载编译
find_library(CAPSTONE_LIB capstone)
find_path(CAPSTONE_INCLUDE capstone/capstone.h)

if(NOT CAPSTONE_LIB OR NOT CAPSTONE_INCLUDE)
    message(STATUS "System capstone not found, building from source...")
    FetchContent_Declare(capstone
        GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
        GIT_TAG        5.0.1
        GIT_SHALLOW    TRUE
    )
    set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(CAPSTONE_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(capstone)
    set(CAPSTONE_LIB capstone)
    set(CAPSTONE_INCLUDE ${capstone_SOURCE_DIR}/include)
endif()

# ── Keystone (汇编) ──
find_library(KEYSTONE_LIB keystone)
find_path(KEYSTONE_INCLUDE keystone/keystone.h)

if(NOT KEYSTONE_LIB OR NOT KEYSTONE_INCLUDE)
    message(STATUS "System keystone not found, building from source...")
    FetchContent_Declare(keystone
        GIT_REPOSITORY https://github.com/keystone-engine/keystone.git
        GIT_TAG        0.9.2
        GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LLVM_TARGETS_TO_BUILD "AArch64" CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(keystone)
    set(KEYSTONE_LIB keystone)
    set(KEYSTONE_INCLUDE ${keystone_SOURCE_DIR}/include)
endif()

# ── TsEngine ──
file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CAPSTONE_INCLUDE}
    ${KEYSTONE_INCLUDE}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -O2
    -ffunction-sections -fdata-sections
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CAPSTONE_LIB}
    ${KEYSTONE_LIB}
)

target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections
)
