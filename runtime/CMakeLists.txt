
add_library(
  Raptor-RT-${LLVM_VERSION_MAJOR}
  obj/Counting.cpp
  obj/GarbageCollection.cpp
  ir/Mpfr.cpp
  ir/Fprt.cpp
  ir/Log.cpp
)

# add_library(
#   Raptor-RT-GC-${LLVM_VERSION_MAJOR}
#   obj/GarbageCollection.cpp
# )
#
# add_library(
#   Raptor-RT-Count-${LLVM_VERSION_MAJOR}
#   obj/Counting.cpp
# )
#
# add_library(
#   Raptor-RT-FP-${LLVM_VERSION_MAJOR}
#   OBJECT
#   ir/Mpfr.cpp
# )
#
# set_target_properties(
#   Raptor-RT-FP-${LLVM_VERSION_MAJOR}
#   PROPERTIES
#   COMPILE_FLAGS "-emit-llvm"
#   OUTPUT_NAME "Raptor-RT-FP-${LLVM_VERSION_MAJOR}"
# )

set(RAPTOR_ALL_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/public
  ${CMAKE_CURRENT_SOURCE_DIR}/include/private
)

target_include_directories(Raptor-RT-${LLVM_VERSION_MAJOR} PRIVATE ${RAPTOR_ALL_INCLUDE_DIRS})
# target_include_directories(Raptor-RT-GC-${LLVM_VERSION_MAJOR} PRIVATE ${RAPTOR_ALL_INCLUDE_DIRS})
# target_include_directories(Raptor-RT-FP-${LLVM_VERSION_MAJOR} PRIVATE ${RAPTOR_ALL_INCLUDE_DIRS})
# target_include_directories(Raptor-RT-Count-${LLVM_VERSION_MAJOR} PRIVATE ${RAPTOR_ALL_INCLUDE_DIRS})

install(TARGETS Raptor-RT-${LLVM_VERSION_MAJOR}
  LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT Raptor-RT-${LLVM_VERSION_MAJOR}
  ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT Raptor-RT-${LLVM_VERSION_MAJOR}
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Raptor-RT-${LLVM_VERSION_MAJOR})

# option(DISABLE_TRUNC_FLOP_COUNT "Disable counters for truncated flops." OFF)
# if(DISABLE_TRUNC_FLOP_COUNT)
#   add_definitions(-DRAPTOR_FPRT_DISABLE_TRUNC_FLOP_COUNT)
# endif(DISABLE_TRUNC_FLOP_COUNT)
