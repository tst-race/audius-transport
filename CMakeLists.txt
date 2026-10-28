cmake_minimum_required(VERSION 3.20)
project(audius-transport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

set(BUILD_VERSION "" CACHE STRING "Version of the plugin being built")
if("${BUILD_VERSION}" STREQUAL "")
    message(SEND_ERROR "Missing or empty BUILD_VERSION argument")
endif()

# Everything the plugin needs (nlohmann_json, gtest/gmock, curl, race SDK headers) is already
# installed in the raceboat-plugin-builder image under /linux/<arch>. Do NOT use FetchContent:
# it needs network access and drags in large third-party builds.

add_subdirectory(source)

# Unit tests are built only on request: cmake -DBUILD_UNIT_TESTS=ON (see README.md)
option(BUILD_UNIT_TESTS "Build the gtest unit tests" OFF)
if(BUILD_UNIT_TESTS)
    enable_testing()
    add_subdirectory(test/source)
endif()
