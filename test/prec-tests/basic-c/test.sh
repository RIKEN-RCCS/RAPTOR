#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

mkdir -p "${SCRIPT_DIR}/build"

clang++ -flto=full -fuse-ld=lld -Wl,-mllvm -Wl,-print-after-all "${SCRIPT_DIR}/src/main.c" $(pkg-config --libs --cflags mpfr gmp) -fno-exceptions -fpass-plugin=/scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/ClangEnzyme-20.so -Xclang -load -Xclang /scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/ClangEnzyme-20.so -Wl,--load-pass-plugin=/scratch/fhrold/riken/Enzyme/enzyme/build/Enzyme/LLDEnzyme-20.so -Rpass=enzyme -mllvm -enzyme-truncate-all=64to2-2 -include enzyme/fprt/mpfr.h -o "${SCRIPT_DIR}/build/enzyme"

clang "${SCRIPT_DIR}/src/main.c" -o "${SCRIPT_DIR}/build/normal"

echo "Normal:   " $("${SCRIPT_DIR}/build/normal" 0.1 0.2)
echo "Truncated:" $("${SCRIPT_DIR}/build/enzyme" 0.1 0.2)
