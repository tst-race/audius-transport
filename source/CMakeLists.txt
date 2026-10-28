################################################################################
# PLUGIN_NAME is the kit directory name, the manifest "file_path", and the directory the
# integration test mounts under /kits. test/adapter.py (KIT_NAME) must use the same value.
################################################################################
set(PLUGIN_NAME PluginAudius)

################################################################################
# Library dependencies. The race SDK headers/libs and nlohmann_json come from the builder
# image's sysroot (/linux/<arch>), found through the toolchain file selected by the preset.
################################################################################

find_library(LIB_CURL curl)
find_library(LIB_CRYPTO crypto)
find_library(LIB_SSL ssl)
find_package(Threads REQUIRED)

set(LIB_DEPS ${LIB_CURL} ${LIB_CRYPTO} ${LIB_SSL} Threads::Threads)

################################################################################
# Kit layout: kit/artifacts/<node-type>-{client,server}/<PLUGIN_NAME>/
#   lib<target>.so, manifest.json, config/*.json
# The integration-test adapter and generate_scenario.py read exactly this layout.
################################################################################

set(PLUGIN_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../kit)
set(COMMON_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)

if(ANDROID)
    if("${TARGET_ARCHITECTURE}" STREQUAL "ANDROID_arm64-v8a")
        set(KIT_DIRS android-arm64-v8a-client)
    else()
        set(KIT_DIRS android-x86_64-client)
    endif()
else()
    if("${TARGET_ARCHITECTURE}" STREQUAL "LINUX_arm64-v8a")
        set(NODE_TYPE linux-arm64-v8a)
    else()
        set(NODE_TYPE linux-x86_64)
    endif()
    set(KIT_DIRS ${NODE_TYPE}-client ${NODE_TYPE}-server)
endif()

function(setup_component_target)
    cmake_parse_arguments(COMPONENT "" "TARGET" "SOURCES" ${ARGN})

    add_library(${COMPONENT_TARGET} SHARED ${COMPONENT_SOURCES})
    target_include_directories(${COMPONENT_TARGET} PRIVATE ${COMMON_SRC_DIR})
    target_link_libraries(${COMPONENT_TARGET} ${LIB_DEPS})
    target_compile_definitions(${COMPONENT_TARGET} PUBLIC BUILD_VERSION="${BUILD_VERSION}")
    set_target_properties(${COMPONENT_TARGET} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        NO_SONAME ON
    )

    install(TARGETS ${COMPONENT_TARGET} LIBRARY DESTINATION lib/race/comms/${PLUGIN_NAME})

    foreach(KIT_DIR ${KIT_DIRS})
        add_custom_command(TARGET ${COMPONENT_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PLUGIN_BASE_DIR}/artifacts/${KIT_DIR}/${PLUGIN_NAME}
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${COMPONENT_TARGET}>
                    ${PLUGIN_BASE_DIR}/artifacts/${KIT_DIR}/${PLUGIN_NAME}/
        )
    endforeach()
endfunction()

################################################################################
# Manifest and action files (copied into every kit directory on every build)
################################################################################

set(CONFIG_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../config)

add_custom_target(${PLUGIN_NAME}KitFiles ALL)
foreach(KIT_DIR ${KIT_DIRS})
    add_custom_command(TARGET ${PLUGIN_NAME}KitFiles POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PLUGIN_BASE_DIR}/artifacts/${KIT_DIR}/${PLUGIN_NAME}/config
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
                ${PLUGIN_BASE_DIR}/artifacts/${KIT_DIR}/${PLUGIN_NAME}/manifest.json
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CONFIG_SRC_DIR}
                ${PLUGIN_BASE_DIR}/artifacts/${KIT_DIR}/${PLUGIN_NAME}/config
    )
endforeach()

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json DESTINATION lib/race/comms/${PLUGIN_NAME})

add_subdirectory(transport)
