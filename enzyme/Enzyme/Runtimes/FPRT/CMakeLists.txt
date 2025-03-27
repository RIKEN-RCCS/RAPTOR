
add_library( Enzyme-FPRT-GC-${LLVM_VERSION_MAJOR}
  GarbageCollection.cpp
)

add_library( Enzyme-FPRT-Count-${LLVM_VERSION_MAJOR}
  Counting.cpp
)

target_include_directories(Enzyme-FPRT-GC-${LLVM_VERSION_MAJOR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

target_include_directories(Enzyme-FPRT-Count-${LLVM_VERSION_MAJOR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)
