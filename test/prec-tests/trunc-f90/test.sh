#!/usr/bin/env sh

set -x
set -e

RAPTOR_BUILD_DIR="/scratch/fhrold/raptor/RAPTOR"

# cp /scratch/fhrold/raptor/Raptor/raptor/include/raptor/fprt/mpfr.h "./mpfr.cpp"
# clang++ -c "./mpfr.cpp" $(pkg-config --cflags mpfr gmp) \
#     -I/scratch/fhrold/raptor/Raptor/raptor/include/raptor/fprt/ \
#     -DRAPTOR_FPRT_ENABLE_GARBAGE_COLLECTION \
#     -DRAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS \
#     -o "./mpfr.o"

# Truncate all
# flang-new -fpass-plugin=${RAPTOR_BUILD_DIR}/Raptor/LLVMRaptor-20.so \
#     -Xflang -load -Xflang ${RAPTOR_BUILD_DIR}/Raptor/LLVMRaptor-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=raptor -mllvm -raptor-truncate-all="64to2-2"  mpfr.o ./src/simple_math.f ./src/main.f -o ./main-flang

# LTO
# flang-new -fpass-plugin=${RAPTOR_BUILD_DIR}/Raptor/LLVMRaptor-20.so \
#     -Xflang -load -Xflang ${RAPTOR_BUILD_DIR}/Raptor/LLVMRaptor-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=raptor  mpfr.o ./src/simple_math.f ./src/main.f -o ./main-flang

# flang-new -O2 -c ./src/simple_math.f90 -flto=full
# flang-new -O2 -c ./src/main_patch.f90 -flto=full
flang-new -O2 -c ./src/main.f90 -flto=full

RAPTOR_DUMP_MODULE_PRE=raptor_pre.ll \
flang-new -O2 main.o -o ./main-flang \
    -flto=full -fuse-ld=lld \
    -Wl,--load-pass-plugin=${RAPTOR_BUILD_DIR}/build/pass/LLVMRaptor-20.so \
    -Wl,-mllvm -Wl,-load=${RAPTOR_BUILD_DIR}/build/pass/LLVMRaptor-20.so \
    -Wl,-mllvm -Wl,-raptor-truncate-count=1 \
    -Wl,-mllvm -Wl,-raptor-truncate-access-count=1 \
    -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
    -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp -flto \
    -lstdc++ \
    -DRAPTOR_FPRT_ENABLE_GARBAGE_COLLECTION \
    -DRAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS \
    -L${RAPTOR_BUILD_DIR}/build/runtime/ \
    -lRaptor-RT-20 \
    -Wl,--allow-multiple-definition
    # -lRaptor-FPRT-GC-20 \
    # -lRaptor-FPRT-Count-20 \
    # -Wl,-mllvm -Wl,-load=${RAPTOR_BUILD_DIR}/build/pass/LLVMRaptor-20.so \
    # -Wl,-mllvm -Wl,-raptor-truncate-count=0 \
    # -Wl,-mllvm -Wl,-raptor-truncate-access-count=0 \

# mpif90 -O2 mpfr.o simple_math.o main_patch.o -o ./main-flang \
#     -flto=full -fuse-ld=lld \
#     -fpass-plugin=${RAPTOR_BUILD_DIR}Raptor/LLVMRaptor-20.so \
#     -Xflang -load -Xflang ${RAPTOR_BUILD_DIR}/Raptor/LLVMRaptor-20.so \
#     -Rpass=raptor \
#     -Wl,--load-pass-plugin=${RAPTOR_BUILD_DIR}/Raptor/LLDRaptor-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp -flto \
#     -lstdc++ \
#     -DRAPTOR_FPRT_ENABLE_GARBAGE_COLLECTION \
#     -DRAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS \
#     -L${RAPTOR_BUILD_DIR}/Raptor/Runtimes/FPRT/ \
#     -lRaptor-FPRT-GC-20 \
#     -lRaptor-FPRT-Count-20 \
#     -Wl,--allow-multiple-definition

# flang-new -O2 mpfr.o simple_math.o ./main_patch.o -o ./main-flang -fpass-plugin=/scratch/fhrold/reproducibility/Raptor/raptor/build/Raptor/LLVMRaptor-20.so \
#     -Xflang -load -Xflang /scratch/fhrold/reproducibility/Raptor/raptor/build/Raptor/LLVMRaptor-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=raptor


# flang-new -O2 -fpass-plugin=/scratch/fhrold/reproducibility/Raptor/raptor/build/Raptor/LLVMRaptor-20.so \
#     -Xflang -load -Xflang /scratch/fhrold/reproducibility/Raptor/raptor/build/Raptor/LLVMRaptor-20.so \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/mpfr-4.2.1-2aivvsalcuno6mvp2lyuta3glkp3o6v2/lib -lmpfr \
#     -L/scratch/fhrold/spack/opt/spack/linux-rocky9-zen2/gcc-11.4.1/gmp-6.3.0-kyy5q7hr34p4dr2aftntqw2z6pmkc7ja/lib -lgmp \
#     -Rpass=raptor  mpfr.o simple_math.o ./src/main_patch.f90 -o ./main-flang
