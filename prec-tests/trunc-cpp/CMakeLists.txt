cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(trunc-cpp CXX)

find_package(PkgConfig)
pkg_check_modules(MPFR REQUIRED mpfr gmp)

find_package(Enzyme REQUIRED CONFIG)

add_executable(enzyme src/main_enzyme.cpp)

# Enzyme truncate all
# target_compile_options(enzyme PUBLIC "-fno-exceptions" "-fpass-plugin=/scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so" "-Xclang" "-load" "SHELL:-Xclang" "/scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so" "-Rpass=enzyme" "-include" "enzyme/fprt/mpfr.h" "-mllvm" "-enzyme-truncate-all=64to2-2")
# target_include_directories(enzyme PUBLIC ${MPFR_INCLUDE_DIRS} "/scratch/fhrold/riken/Enzyme/enzyme/include")
# target_link_libraries(enzyme PUBLIC ${MPFR_LINK_LIBRARIES})

# Enzyme truncate LTO
target_compile_definitions(enzyme PUBLIC -DNOVERSION -DLTO_TRUNC)
target_compile_options(enzyme PUBLIC "-include" "enzyme/fprt/mpfr.h" "-Rpass=enzyme")
target_link_libraries(enzyme PUBLIC LLDEnzymeFlags ${MPFR_LINK_LIBRARIES})
target_link_options(enzyme PUBLIC "-fno-exceptions" "-Rpass=enzyme")
# target_link_options(enzyme PUBLIC "-flto=full" "-fuse-ld=lld" "-Wl,-mllvm" "-Wl,-print-after-all")
target_include_directories(enzyme PUBLIC ${MPFR_INCLUDE_DIRS} "/scratch/fhrold/riken/Enzyme/enzyme/include")
