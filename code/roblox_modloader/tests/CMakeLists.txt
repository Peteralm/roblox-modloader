file(GLOB_RECURSE RML_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(rml_unit_tests ${RML_UNIT_TEST_SOURCES})

target_include_directories(rml_unit_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${ROBLOX_MODLOADER_SOURCE_DIR}"
        "${ROBLOX_MODLOADER_ROOT_DIR}/libs/third_party/g3d/include"
        "${ROBLOX_MODLOADER_ROOT_DIR}/libs/third_party/rbxg3d/include"
        "${ROBLOX_MODLOADER_ROOT_DIR}/libs/third_party/bullet/include"
)
target_link_libraries(rml_unit_tests PRIVATE
        doctest::doctest
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        tomlplusplus::tomlplusplus
        Luau.VM
)
target_compile_features(rml_unit_tests PRIVATE cxx_std_23)

# The catalog and load-observer tests exercise loader translation units directly instead of
# linking the loader, which is a DLL built against the engine.
target_sources(rml_unit_tests PRIVATE
        "${ROBLOX_MODLOADER_SOURCE_DIR}/config/config_serialization.cpp"
        "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/mod_catalog.cpp"
        "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/mod_kind.cpp"
        "${ROBLOX_MODLOADER_SOURCE_DIR}/luau/load_observer.cpp"
)

if (WIN32)
    target_compile_definitions(rml_unit_tests PRIVATE ROBLOX_MODLOADER_STATIC_DEFINE)
endif ()

setup_compiler_flags(rml_unit_tests)
setup_compile_definitions(rml_unit_tests PRIVATE)

set_target_properties(rml_unit_tests PROPERTIES FOLDER "Tests")

add_test(NAME rml_unit_tests COMMAND rml_unit_tests)
