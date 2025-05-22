configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(RAPTOR_TEST_DEPS LLVMRaptor-${LLVM_VERSION_MAJOR} Raptor-RT-${LLVM_VERSION_MAJOR})

add_subdirectory(Unit)
if (${Clang_FOUND})
  add_subdirectory(Integration)
endif()
