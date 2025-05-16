#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

mkdir -p "${SCRIPT_DIR}/build"

cp /scratch/fhrold/riken/Raptor/raptor/include/raptor/fprt/mpfr.h "${SCRIPT_DIR}/src/mpfr.cpp"
clang++ -c "${SCRIPT_DIR}/src/mpfr.cpp" $(pkg-config --cflags mpfr gmp) \
    -I/scratch/fhrold/riken/Raptor/raptor/include/raptor/fprt/ -o "${SCRIPT_DIR}/build/mpfr.o"

flang-new "${SCRIPT_DIR}/build/mpfr.o" "${SCRIPT_DIR}/src/main.f90" \
    $(pkg-config --libs --cflags mpfr gmp) \
    -fpass-plugin=/scratch/fhrold/riken/Raptor/raptor/build/Raptor/LLVMRaptor-20.so \
    -Xflang -load -Xflang /scratch/fhrold/riken/Raptor/raptor/build/Raptor/LLVMRaptor-20.so \
    -Rpass=raptor -mllvm -raptor-truncate-all="64to2-2" -o "${SCRIPT_DIR}/build/raptor"

flang-new "${SCRIPT_DIR}/src/main.f90" -o "${SCRIPT_DIR}/build/normal"

echo "Normal:   " $(./build/normal 0.1 0.2)
echo "Truncated:" $(./build/raptor 0.1 0.2)
