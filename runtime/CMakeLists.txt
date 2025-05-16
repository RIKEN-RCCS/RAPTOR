
add_library( Raptor-FPRT-GC-${LLVM_VERSION_MAJOR}
  GarbageCollection.cpp
)

add_library( Raptor-FPRT-Count-${LLVM_VERSION_MAJOR}
  Counting.cpp
)

target_include_directories(Raptor-FPRT-GC-${LLVM_VERSION_MAJOR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

target_include_directories(Raptor-FPRT-Count-${LLVM_VERSION_MAJOR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)

option(DISABLE_TRUNC_FLOP_COUNT "Disable counters for truncated flops." OFF)
if(DISABLE_TRUNC_FLOP_COUNT)
  add_definitions(-DRAPTOR_FPRT_DISABLE_TRUNC_FLOP_COUNT)
endif(DISABLE_TRUNC_FLOP_COUNT)
