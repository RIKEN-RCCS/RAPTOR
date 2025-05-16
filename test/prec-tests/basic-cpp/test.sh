#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

mkdir -p "${SCRIPT_DIR}/build"

clang++ "${SCRIPT_DIR}/src/main.cpp" $(pkg-config --libs --cflags mpfr gmp) -fno-exceptions -fpass-plugin=/scratch/fhrold/riken/Raptor/raptor/build/Raptor/ClangRaptor-20.so -Xclang -load -Xclang /scratch/fhrold/riken/Raptor/raptor/build/Raptor/ClangRaptor-20.so -Rpass=raptor -mllvm -raptor-truncate-all="64to2-2" -include raptor/fprt/mpfr.h -o "${SCRIPT_DIR}/build/raptor"

clang++ "${SCRIPT_DIR}/src/main.cpp" -o "${SCRIPT_DIR}/build/normal"

echo "Normal:   " $("${SCRIPT_DIR}/build/normal" 0.1 0.2)
echo "Truncated:" $("${SCRIPT_DIR}/build/raptor" 0.1 0.2)

