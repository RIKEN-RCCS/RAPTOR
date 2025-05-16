#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

mkdir -p "${SCRIPT_DIR}/build"

cp /scratch/fhrold/riken/Enzyme/enzyme/include/enzyme/fprt/mpfr.h "${SCRIPT_DIR}/src/mpfr.cpp"
clang++ -c "${SCRIPT_DIR}/src/mpfr.cpp" $(pkg-config --cflags mpfr gmp) \
    -I/scratch/fhrold/riken/Enzyme/enzyme/include/enzyme/fprt/ -o "${SCRIPT_DIR}/build/mpfr.o"

flang-new "${SCRIPT_DIR}/build/mpfr.o" "${SCRIPT_DIR}/src/main.f90" \
    $(pkg-config --libs --cflags mpfr gmp) \
    -fpass-plugin=/scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so \
    -Xflang -load -Xflang /scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so \
    -Rpass=enzyme -mllvm -enzyme-truncate-all="64to2-2" -o "${SCRIPT_DIR}/build/enzyme"

flang-new "${SCRIPT_DIR}/src/main.f90" -o "${SCRIPT_DIR}/build/normal"

echo "Normal:   " $(./build/normal 0.1 0.2)
echo "Truncated:" $(./build/enzyme 0.1 0.2)
