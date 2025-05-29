
# TODO is this always the correct path?
set(RAPTOR_CLANG_PATH
  ${LLVM_TOOLS_BINARY_DIR}/clang)
set(RAPTOR_CLANGPLUSPLUS_PATH
  ${LLVM_TOOLS_BINARY_DIR}/clang++)
set(RAPTOR_FLANG_PATH
  ${LLVM_TOOLS_BINARY_DIR}/flang)

if (${Clang_FOUND})
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/raptor-clang.in
    ${CMAKE_CURRENT_BINARY_DIR}/raptor-clang
    @ONLY
  )
  install(PROGRAMS
    ${CMAKE_CURRENT_BINARY_DIR}/raptor-clang
    DESTINATION bin)

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/raptor-clang++.in
    ${CMAKE_CURRENT_BINARY_DIR}/raptor-clang++
    @ONLY
  )
  install(PROGRAMS
    ${CMAKE_CURRENT_BINARY_DIR}/raptor-clang++
    DESTINATION bin)
endif()

if (${Flang_FOUND})
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/raptor-flang.in
    ${CMAKE_CURRENT_BINARY_DIR}/raptor-flang
    @ONLY
  )
  install(PROGRAMS
    ${CMAKE_CURRENT_BINARY_DIR}/raptor-flang
    DESTINATION bin)
endif()
