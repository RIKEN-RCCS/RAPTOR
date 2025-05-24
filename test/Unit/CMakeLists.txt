# Run regression and unit tests
add_lit_testsuite(check-raptor-pass "Running raptor integration tests"
    ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${RAPTOR_TEST_DEPS} ClangRaptor-${LLVM_VERSION_MAJOR}
    ARGS -v
)
