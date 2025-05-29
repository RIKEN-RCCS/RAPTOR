umbrella_lit_testsuite_begin(check-raptor-integration)

# TODO add checks for flang and lld

if (${Clang_FOUND})
    add_lit_testsuite(check-raptor-integration-truncate-cpp "Running cpp raptor fp truncation integration tests"
        ${CMAKE_CURRENT_BINARY_DIR}/Cpp
        DEPENDS ${RAPTOR_TEST_DEPS} ClangRaptor-${LLVM_VERSION_MAJOR}
        ARGS -v
    )
endif()

if (${Clang_FOUND})
    add_lit_testsuite(check-raptor-integration-truncate-lld "Running lld raptor fp truncation integration tests"
        ${CMAKE_CURRENT_BINARY_DIR}/LLD
        DEPENDS ${RAPTOR_TEST_DEPS} ClangRaptor-${LLVM_VERSION_MAJOR} LLDRaptor-${LLVM_VERSION_MAJOR}
        ARGS -v
    )
endif()

if (${Flang_FOUND})
    add_lit_testsuite(check-raptor-integration-truncate-fortran "Running fortran raptor fp truncation integration tests"
        ${CMAKE_CURRENT_BINARY_DIR}/Fortran
        DEPENDS ${RAPTOR_TEST_DEPS}
        ARGS -v
    )
endif()

umbrella_lit_testsuite_end(check-raptor-integration)
