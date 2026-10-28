# The target name determines the library file name: PluginAudius -> libPluginAudius.so.
# manifest.json "shared_library_path" must match that file name exactly.
set(AUDIUS_TRANSPORT_SOURCES
    PluginAudius.cpp
    Link.cpp
    LinkAddress.cpp
    LinkMap.cpp
    AudiusClient.cpp
    ../common/log.cpp
)

setup_component_target(
    TARGET PluginAudius
    SOURCES ${AUDIUS_TRANSPORT_SOURCES}
)
