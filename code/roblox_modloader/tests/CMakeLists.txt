if (ROBLOX_MODLOADER_BUILD_CORE_TESTS)
    file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    )
    list(FILTER CORE_TEST_SOURCES EXCLUDE REGEX "/fixtures/")
    if (NOT WIN32)
        list(FILTER CORE_TEST_SOURCES EXCLUDE REGEX "/early_mod_registry_test.cpp$")
    endif ()

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

    if (WIN32)
        target_compile_definitions(roblox_modloader_tests PRIVATE ROBLOX_MODLOADER_STATIC_DEFINE
                RML_EARLY_FIXTURE_DIR="$<TARGET_FILE_DIR:early_fixture_success>")
        target_sources(roblox_modloader_tests PRIVATE
                "${ROBLOX_MODLOADER_SOURCE_DIR}/native/early_mod_registry.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/native/native_mod_loader.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/native/mod_registry.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/memory/module.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/memory/range.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/memory/symbol_resolver.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/ModBase.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/mod_paths.cpp"
                "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/events.cpp"
        )
        function(add_early_fixture name)
            add_library(early_fixture_${name} SHARED fixtures/early_mod_fixture.cpp
                    "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/ModBase.cpp"
                    "${ROBLOX_MODLOADER_SOURCE_DIR}/mod/mod_paths.cpp")
            target_include_directories(early_fixture_${name} PRIVATE "${ROBLOX_MODLOADER_INCLUDE_DIR}")
            target_link_libraries(early_fixture_${name} PRIVATE spdlog::spdlog tomlplusplus::tomlplusplus Luau.VM)
            target_compile_definitions(early_fixture_${name} PRIVATE ROBLOX_MODLOADER_STATIC_DEFINE ${ARGN})
            setup_compiler_flags(early_fixture_${name})
            setup_compile_definitions(early_fixture_${name} PRIVATE)
            add_dependencies(roblox_modloader_tests early_fixture_${name})
        endfunction()
        add_early_fixture(success)
        add_early_fixture(missing_start OMIT_START)
        add_early_fixture(missing_uninstall OMIT_UNINSTALL)
        add_early_fixture(missing_normal_abi OMIT_NORMAL_ABI)
        add_early_fixture(missing_early_abi OMIT_EARLY_ABI)
        add_early_fixture(missing_early OMIT_EARLY)
        add_early_fixture(bad_normal_abi BAD_NORMAL_ABI)
        add_early_fixture(bad_early_abi BAD_EARLY_ABI)
        add_early_fixture(normal_abi_seh NORMAL_ABI_SEH)
        add_early_fixture(early_abi_seh EARLY_ABI_SEH)
        add_early_fixture(normal_abi_throw NORMAL_ABI_THROW)
    endif ()

    set_target_properties(roblox_modloader_tests PROPERTIES FOLDER "ModLoader")

    add_test(NAME roblox_modloader_tests COMMAND roblox_modloader_tests)
endif ()
