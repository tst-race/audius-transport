# Built from the top-level CMakeLists.txt when -DBUILD_UNIT_TESTS=ON
# (./build_artifacts.sh --unit-tests). Add every new test file to the list below.
find_package(GTest REQUIRED CONFIG)
find_package(Threads REQUIRED)
find_library(LIB_CURL curl)
# RaceLog and the SDK type helpers live in libraceSdkCommon.
find_library(RACE_SDK_COMMON raceSdkCommon REQUIRED)

set(PLUGIN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../source)

add_executable(unitTestPluginAudius
    ${PLUGIN_SOURCE_DIR}/common/log.cpp
    ${PLUGIN_SOURCE_DIR}/transport/Link.cpp
    ${PLUGIN_SOURCE_DIR}/transport/LinkAddress.cpp
    ${PLUGIN_SOURCE_DIR}/transport/LinkMap.cpp
    ${PLUGIN_SOURCE_DIR}/transport/PluginAudius.cpp
    ${PLUGIN_SOURCE_DIR}/transport/AudiusClient.cpp
    main.cpp
    transport/TestLinkAddress.cpp
    transport/TestLinkMap.cpp
    transport/TestLink.cpp
    transport/TestPluginAudius.cpp
)

# TESTBUILD removes the extern "C" createTransport/destroyTransport entry points.
target_compile_definitions(unitTestPluginAudius PRIVATE TESTBUILD BUILD_VERSION="test")

target_include_directories(unitTestPluginAudius PRIVATE
    common
    ${PLUGIN_SOURCE_DIR}/common
    ${PLUGIN_SOURCE_DIR}/transport
)

target_link_libraries(unitTestPluginAudius
    GTest::gmock
    GTest::gtest
    ${RACE_SDK_COMMON}
    Threads::Threads
    ${LIB_CURL}
    stdc++fs
)

# The builder image's libraceSdkCommon.so was built against a newer glibc than the image ships, so
# the test binary links here but can only RUN in the raceboat-runtime image (see run_unit_tests.sh).
target_link_options(unitTestPluginAudius PRIVATE -Wl,--allow-shlib-undefined)

add_test(NAME unitTestPluginAudius COMMAND unitTestPluginAudius)
