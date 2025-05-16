add_lit_testsuite(check-raptor-integration-truncate "Running raptor fp truncation integration tests"
    ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${RAPTOR_TEST_DEPS}
    ARGS -v
)

set_target_properties(check-raptor-integration-truncate PROPERTIES FOLDER "Tests")

