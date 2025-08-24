cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PROMISE_CC_BUILD_TESTS "Build tests" OFF)

project(promise-cc
VERSION 0.0.4
DESCRIPTION "C++ promise, like JavaScript"
LANGUAGES C CXX
)

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/include)

# alias promise-cc::promise
add_library(promise-cc::promise ALIAS ${PROJECT_NAME})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/promise/version.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/promise/version.h
)

if(PROMISE_CC_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.0 # or a later release
    )
    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
    
    add_subdirectory(test)
endif()