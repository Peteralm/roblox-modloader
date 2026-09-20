if (ROBLOX_MODLOADER_BUILD_CORE_TESTS)
    file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    )

    add_executable(roblox_modloader_tests ${CORE_TEST_SOURCES})

    target_compile_features(roblox_modloader_tests PRIVATE cxx_std_23)

    target_include_directories(roblox_modloader_tests PRIVATE
            "${ROBLOX_MODLOADER_INCLUDE_DIR}"
            "${ROBLOX_MODLOADER_SOURCE_DIR}"
    )

    target_link_libraries(roblox_modloader_tests PRIVATE
            doctest::doctest
            spdlog::spdlog
            nlohmann_json::nlohmann_json
            tomlplusplus::tomlplusplus
            Luau.VM
    )

    target_sources(roblox_modloader_tests PRIVATE
            "${ROBLOX_MODLOADER_SOURCE_DIR}/config/config_serialization.cpp"
            "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/mod_catalog.cpp"
            "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/mod_kind.cpp"
    )

    setup_compiler_flags(roblox_modloader_tests)
    setup_compile_definitions(roblox_modloader_tests PRIVATE)

    set_target_properties(roblox_modloader_tests PROPERTIES FOLDER "ModLoader")

    add_test(NAME roblox_modloader_tests COMMAND roblox_modloader_tests)
endif ()
