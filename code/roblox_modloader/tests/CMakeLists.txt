file(GLOB_RECURSE RML_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(rml_unit_tests ${RML_UNIT_TEST_SOURCES})

target_include_directories(rml_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(rml_unit_tests PRIVATE doctest::doctest)
target_compile_features(rml_unit_tests PRIVATE cxx_std_23)

set_target_properties(rml_unit_tests PROPERTIES FOLDER "Tests")

add_test(NAME rml_unit_tests COMMAND rml_unit_tests)
