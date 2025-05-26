umbrella_lit_testsuite_begin(check-raptor-integration)

add_lit_testsuite(check-raptor-integration-truncate-cpp "Running cpp raptor fp truncation integration tests"
    ${CMAKE_CURRENT_BINARY_DIR}/Cpp
    DEPENDS ${RAPTOR_TEST_DEPS}
    ARGS -v
)

add_lit_testsuite(check-raptor-integration-truncate-fortran "Running fortran raptor fp truncation integration tests"
    ${CMAKE_CURRENT_BINARY_DIR}/Fortran
    DEPENDS ${RAPTOR_TEST_DEPS}
    ARGS -v
)

set_target_properties(check-raptor-integration-truncate PROPERTIES FOLDER "Tests")

umbrella_lit_testsuite_end(check-raptor-integration)
