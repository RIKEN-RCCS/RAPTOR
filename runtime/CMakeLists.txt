
add_library(
  Raptor-RT-GC-${LLVM_VERSION_MAJOR}
  obj/GarbageCollection.cpp
)

add_library(
  Raptor-RT-Count-${LLVM_VERSION_MAJOR}
  obj/Counting.cpp
)

add_library(
  Raptor-RT-FP-${LLVM_VERSION_MAJOR}
  OBJECT
  ir/Mpfr.cpp
)

set_target_properties(
  Raptor-RT-FP-${LLVM_VERSION_MAJOR}
  PROPERTIES
  COMPILE_FLAGS "-emit-llvm"
  OUTPUT_NAME "Raptor-RT-FP-${LLVM_VERSION_MAJOR}"
)

set(ICNLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/public
  ${CMAKE_CURRENT_SOURCE_DIR}/include/private
)
target_include_directories(Raptor-RT-GC-${LLVM_VERSION_MAJOR} PRIVATE ${INCLUDE_DIRS})
target_include_directories(Raptor-RT-FP-${LLVM_VERSION_MAJOR} PRIVATE ${INCLUDE_DIRS})
target_include_directories(Raptor-RT-Count-${LLVM_VERSION_MAJOR} PRIVATE ${INCLUDE_DIRS})

option(DISABLE_TRUNC_FLOP_COUNT "Disable counters for truncated flops." OFF)
if(DISABLE_TRUNC_FLOP_COUNT)
  add_definitions(-DRAPTOR_FPRT_DISABLE_TRUNC_FLOP_COUNT)
endif(DISABLE_TRUNC_FLOP_COUNT)
