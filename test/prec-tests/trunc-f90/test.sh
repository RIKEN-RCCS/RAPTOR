#!/usr/bin/env sh

set -x
set -e

ENZYME_BUILD_DIR="/scratch/fhrold/reproducibility/Enzyme/enzyme/build"

cp /scratch/fhrold/reproducibility/Enzyme/enzyme/include/enzyme/fprt/mpfr.h "./mpfr.cpp"
clang++ -c "./mpfr.cpp" $(pkg-config --cflags mpfr gmp) \
    -I/scratch/fhrold/reproducibility/Enzyme/enzyme/include/enzyme/fprt/ \
    -DENZYME_FPRT_ENABLE_GARBAGE_COLLECTION \
    -DENZYME_FPRT_ENABLE_SHADOW_RESIDUALS \
    -o "./mpfr.o"

# Truncate all
# flang-new -fpass-plugin=${ENZYME_BUILD_DIR}/Enzyme/LLVMEnzyme-20.so \
#     -Xflang -load -Xflang ${ENZYME_BUILD_DIR}/Enzyme/LLVMEnzyme-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=enzyme -mllvm -enzyme-truncate-all="64to2-2"  mpfr.o ./src/simple_math.f ./src/main.f -o ./main-flang

# LTO
# flang-new -fpass-plugin=${ENZYME_BUILD_DIR}/Enzyme/LLVMEnzyme-20.so \
#     -Xflang -load -Xflang ${ENZYME_BUILD_DIR}/Enzyme/LLVMEnzyme-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=enzyme  mpfr.o ./src/simple_math.f ./src/main.f -o ./main-flang

flang-new -O2 -c ./src/simple_math.f90 -flto=full
flang-new -O2 -c ./src/main_patch.f90 -flto=full

ENZYME_DUMP_MODULE_PRE=enzyme_pre.ll \
mpif90 -O2 mpfr.o simple_math.o main_patch.o -o ./main-flang \
    -flto=full -fuse-ld=lld \
    -Wl,--load-pass-plugin=${ENZYME_BUILD_DIR}/Enzyme/LLDEnzyme-20.so \
    -Wl,-mllvm -Wl,-load=${ENZYME_BUILD_DIR}/Enzyme/LLDEnzyme-20.so \
    -Wl,-mllvm -Wl,-enzyme-truncate-count=1 \
    -Wl,-mllvm -Wl,-enzyme-truncate-access-count=1 \
    -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
    -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp -flto \
    -lstdc++ \
    -DENZYME_FPRT_ENABLE_GARBAGE_COLLECTION \
    -DENZYME_FPRT_ENABLE_SHADOW_RESIDUALS \
    -L${ENZYME_BUILD_DIR}/Enzyme/Runtimes/FPRT/ \
    -lEnzyme-FPRT-GC-20 \
    -lEnzyme-FPRT-Count-20 \
    -Wl,--allow-multiple-definition

# mpif90 -O2 mpfr.o simple_math.o main_patch.o -o ./main-flang \
#     -flto=full -fuse-ld=lld \
#     -fpass-plugin=${ENZYME_BUILD_DIR}Enzyme/LLVMEnzyme-20.so \
#     -Xflang -load -Xflang ${ENZYME_BUILD_DIR}/Enzyme/LLVMEnzyme-20.so \
#     -Rpass=enzyme \
#     -Wl,--load-pass-plugin=${ENZYME_BUILD_DIR}/Enzyme/LLDEnzyme-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp -flto \
#     -lstdc++ \
#     -DENZYME_FPRT_ENABLE_GARBAGE_COLLECTION \
#     -DENZYME_FPRT_ENABLE_SHADOW_RESIDUALS \
#     -L${ENZYME_BUILD_DIR}/Enzyme/Runtimes/FPRT/ \
#     -lEnzyme-FPRT-GC-20 \
#     -lEnzyme-FPRT-Count-20 \
#     -Wl,--allow-multiple-definition

# flang-new -O2 mpfr.o simple_math.o ./main_patch.o -o ./main-flang -fpass-plugin=/scratch/fhrold/reproducibility/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so \
#     -Xflang -load -Xflang /scratch/fhrold/reproducibility/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=enzyme


# flang-new -O2 -fpass-plugin=/scratch/fhrold/reproducibility/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so \
#     -Xflang -load -Xflang /scratch/fhrold/reproducibility/Enzyme/enzyme/build/Enzyme/LLVMEnzyme-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=enzyme  mpfr.o simple_math.o ./src/main_patch.f90 -o ./main-flang
