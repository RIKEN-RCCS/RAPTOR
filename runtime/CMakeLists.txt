
add_library( Raptor-RT-GC-${LLVM_VERSION_MAJOR}
  obj/GarbageCollection.cpp
)

add_library( Raptor-RT-Count-${LLVM_VERSION_MAJOR}
  obj/Counting.cpp
)

target_include_directories(Raptor-RT-GC-${LLVM_VERSION_MAJOR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_include_directories(Raptor-RT-Count-${LLVM_VERSION_MAJOR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

option(DISABLE_TRUNC_FLOP_COUNT "Disable counters for truncated flops." OFF)
if(DISABLE_TRUNC_FLOP_COUNT)
  add_definitions(-DRAPTOR_FPRT_DISABLE_TRUNC_FLOP_COUNT)
endif(DISABLE_TRUNC_FLOP_COUNT)
