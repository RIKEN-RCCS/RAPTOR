cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(trunc-cpp CXX)

find_package(PkgConfig)
pkg_check_modules(MPFR REQUIRED mpfr gmp)

find_package(Raptor REQUIRED CONFIG)

add_executable(raptor src/main_raptor.cpp)

# Raptor truncate all
# target_compile_options(raptor PUBLIC "-fno-exceptions" "-fpass-plugin=/scratch/fhrold/riken/Raptor/raptor/build/Raptor/LLVMRaptor-20.so" "-Xclang" "-load" "SHELL:-Xclang" "/scratch/fhrold/riken/Raptor/raptor/build/Raptor/LLVMRaptor-20.so" "-Rpass=raptor" "-include" "raptor/fprt/mpfr.h" "-mllvm" "-raptor-truncate-all=64to2-2")
# target_include_directories(raptor PUBLIC ${MPFR_INCLUDE_DIRS} "/scratch/fhrold/riken/Raptor/raptor/include")
# target_link_libraries(raptor PUBLIC ${MPFR_LINK_LIBRARIES})

# Raptor truncate LTO
target_compile_definitions(raptor PUBLIC -DNOVERSION -DLTO_TRUNC)
target_compile_options(raptor PUBLIC "-include" "raptor/fprt/mpfr.h" "-Rpass=raptor")
target_link_libraries(raptor PUBLIC LLDRaptorFlags ${MPFR_LINK_LIBRARIES})
target_link_options(raptor PUBLIC "-fno-exceptions" "-Rpass=raptor")
# target_link_options(raptor PUBLIC "-flto=full" "-fuse-ld=lld" "-Wl,-mllvm" "-Wl,-print-after-all")
target_include_directories(raptor PUBLIC ${MPFR_INCLUDE_DIRS} "/scratch/fhrold/riken/Raptor/raptor/include")
