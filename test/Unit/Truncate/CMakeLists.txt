# Run regression and unit tests
add_lit_testsuite(check-raptor-trunc "Running raptor truncation tests"
    ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${RAPTOR_TEST_DEPS}
    ARGS -v
)

set_target_properties(check-raptor-trunc PROPERTIES FOLDER "Tests")

# add_lit_testsuites(RAPTOR ${CMAKE_CURRENT_SOURCE_DIR}
# DEPENDS ${RAPTOR_TEST_DEPS}
# )
