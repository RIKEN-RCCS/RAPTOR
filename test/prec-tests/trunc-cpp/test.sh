#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

mkdir -p "${SCRIPT_DIR}/build"

cmake -DCMAKE_CXX_COMPILER=clang++ -B "${SCRIPT_DIR}/build" -S "${SCRIPT_DIR}/"
cmake --build "${SCRIPT_DIR}/build"

echo "Normal:   " $("${SCRIPT_DIR}/build/normal" 0.1 0.2)
echo "Truncated:" $("${SCRIPT_DIR}/build/raptor" 0.1 0.2)

