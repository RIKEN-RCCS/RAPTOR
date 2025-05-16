//===- fprt/mpfr - MPFR wrappers ---------------------------------------===//
//
//                             Raptor Project
//
// Part of the Raptor Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// If using this code in an academic setting, please cite the following:
// @incollection{raptorNeurips,
// title = {Instead of Rewriting Foreign Code for Machine Learning,
//          Automatically Synthesize Fast Gradients},
// author = {Moses, William S. and Churavy, Valentin},
// booktitle = {Advances in Neural Information Processing Systems 33},
// year = {2020},
// note = {To appear in},
// }
//
//===----------------------------------------------------------------------===//
//
// This file contains easy to use wrappers around MPFR functions.
//
//===----------------------------------------------------------------------===//
#ifndef __RAPTOR_RUNTIME_RAPTOR_MPFR__
#define __RAPTOR_RUNTIME_RAPTOR_MPFR__

#include <mpfr.h>
#include <stdint.h>
#include <stdlib.h>

#include "fprt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __RAPTOR_MPFR_ATTRIBUTES __attribute__((weak))
#define __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES __attribute__((weak))
#define __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE GMP_RNDN

typedef struct __raptor_fp {
  mpfr_t result;
} __raptor_fp;

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_64_52_get(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  printf("%p, %s\n", loc, loc);
  __raptor_fp *a = __raptor_fprt_double_to_ptr(_a);
  return mpfr_get_d(a->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);
}

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_64_52_new(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  printf("%p, %s\n", loc, loc);
  __raptor_fp *a = (__raptor_fp *)malloc(sizeof(__raptor_fp));
  mpfr_init2(a->result, significand);
  mpfr_set_d(a->result, _a, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);
  return __raptor_fprt_ptr_to_double(a);
}

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_64_52_const(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc) {
  printf("%p, %s\n", loc, loc);
  // TODO This should really be called only once for an appearance in the code,
  // currently it is called every time a flop uses a constant.
  return __raptor_fprt_64_52_new(_a, exponent, significand, mode, loc);
}

__RAPTOR_MPFR_ATTRIBUTES
__raptor_fp *__raptor_fprt_64_52_new_intermediate(int64_t exponent,
                                                  int64_t significand,
                                                  int64_t mode,
                                                  const char *loc) {
  printf("%p, %s\n", loc, loc);
  __raptor_fp *a = (__raptor_fp *)malloc(sizeof(__raptor_fp));
  mpfr_init2(a->result, significand);
  return a;
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_64_52_delete(double a, int64_t exponent, int64_t significand,
                                int64_t mode, const char *loc) {
  printf("%p, %s\n", loc, loc);
  free(__raptor_fprt_double_to_ptr(a));
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_64_52_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  printf("DOUBLE\n");
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_32_23_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  printf("FLOAT\n");
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_16_10_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  printf("HALF\n");
}

#define __RAPTOR_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc) {                                                       \
    printf("%p, %s, %s\n", loc, #LLVM_OP_NAME, loc);                           \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_t ma, mc;                                                           \
      mpfr_init2(ma, significand);                                             \
      mpfr_init2(mc, significand);                                             \
      mpfr_set_##MPFR_SET_ARG1(ma, a, ROUNDING_MODE);                          \
      mpfr_##MPFR_FUNC_NAME(mc, ma, ROUNDING_MODE);                            \
      RET c = mpfr_get_##MPFR_GET(mc, ROUNDING_MODE);                          \
      mpfr_clear(ma);                                                          \
      mpfr_clear(mc);                                                          \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_double_to_ptr(a);                        \
      __raptor_fp *mc = __raptor_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, ROUNDING_MODE);            \
      return __raptor_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __RAPTOR_MPFR_BIN_INT(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME,           \
                              FROM_TYPE, RET, MPFR_GET, ARG1, MPFR_SET_ARG1,   \
                              ARG2, ROUNDING_MODE)                             \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc) {                                                       \
    printf("%p, %s, %s\n", loc, #LLVM_OP_NAME, loc);                           \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_t ma, mc;                                                           \
      mpfr_init2(ma, significand);                                             \
      mpfr_init2(mc, significand);                                             \
      mpfr_set_##MPFR_SET_ARG1(ma, a, ROUNDING_MODE);                          \
      mpfr_##MPFR_FUNC_NAME(mc, ma, b, ROUNDING_MODE);                         \
      RET c = mpfr_get_##MPFR_GET(mc, ROUNDING_MODE);                          \
      mpfr_clear(ma);                                                          \
      mpfr_clear(mc);                                                          \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_double_to_ptr(a);                        \
      __raptor_fp *mc = __raptor_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, b, ROUNDING_MODE);         \
      return __raptor_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_BIN(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE,    \
                          RET, MPFR_GET, ARG1, MPFR_SET_ARG1, ARG2,            \
                          MPFR_SET_ARG2, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc) {                                                       \
    printf("%p, %s, %s\n", loc, #LLVM_OP_NAME, loc);                           \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_t ma, mb, mc;                                                       \
      mpfr_init2(ma, significand);                                             \
      mpfr_init2(mb, significand);                                             \
      mpfr_init2(mc, significand);                                             \
      mpfr_set_##MPFR_SET_ARG1(ma, a, ROUNDING_MODE);                          \
      mpfr_set_##MPFR_SET_ARG2(mb, b, ROUNDING_MODE);                          \
      mpfr_##MPFR_FUNC_NAME(mc, ma, mb, ROUNDING_MODE);                        \
      RET c = mpfr_get_##MPFR_GET(mc, ROUNDING_MODE);                          \
      mpfr_clear(ma);                                                          \
      mpfr_clear(mb);                                                          \
      mpfr_clear(mc);                                                          \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_double_to_ptr(a);                        \
      __raptor_fp *mb = __raptor_fprt_double_to_ptr(b);                        \
      __raptor_fp *mc = __raptor_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, mb->result,                \
                            ROUNDING_MODE);                                    \
      return __raptor_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,        \
                              LLVM_TYPE, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  TYPE __raptor_fprt_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(          \
      TYPE a, TYPE b, TYPE c, int64_t exponent, int64_t significand,           \
      int64_t mode, const char *loc) {                                         \
    printf("%p, %s, %s\n", loc, #LLVM_OP_NAME, loc);                           \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_t ma, mb, mc, mmul, madd;                                           \
      mpfr_init2(ma, significand);                                             \
      mpfr_init2(mb, significand);                                             \
      mpfr_init2(mc, significand);                                             \
      mpfr_init2(mmul, significand);                                           \
      mpfr_init2(madd, significand);                                           \
      mpfr_set_##MPFR_TYPE(ma, a, ROUNDING_MODE);                              \
      mpfr_set_##MPFR_TYPE(mb, b, ROUNDING_MODE);                              \
      mpfr_set_##MPFR_TYPE(mc, c, ROUNDING_MODE);                              \
      mpfr_mul(mmul, ma, mb, ROUNDING_MODE);                                   \
      mpfr_add(madd, mmul, mc, ROUNDING_MODE);                                 \
      TYPE res = mpfr_get_##MPFR_TYPE(madd, ROUNDING_MODE);                    \
      mpfr_clear(ma);                                                          \
      mpfr_clear(mb);                                                          \
      mpfr_clear(mc);                                                          \
      mpfr_clear(mmul);                                                        \
      mpfr_clear(madd);                                                        \
      return res;                                                              \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_double_to_ptr(a);                        \
      __raptor_fp *mb = __raptor_fprt_double_to_ptr(b);                        \
      __raptor_fp *mc = __raptor_fprt_double_to_ptr(c);                        \
      double mmul = __raptor_fprt_##FROM_TYPE##_binop_fmul(                    \
          __raptor_fprt_ptr_to_double(ma), __raptor_fprt_ptr_to_double(mb),    \
          exponent, significand, mode, loc);                                   \
      double madd = __raptor_fprt_##FROM_TYPE##_binop_fadd(                    \
          mmul, __raptor_fprt_ptr_to_double(mc), exponent, significand, mode,  \
          loc);                                                                \
      return madd;                                                             \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

// TODO This does not currently make distinctions between ordered/unordered.
#define __RAPTOR_MPFR_FCMP_IMPL(NAME, ORDERED, CMP, FROM_TYPE, TYPE, MPFR_GET, \
                                ROUNDING_MODE)                                 \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  bool __raptor_fprt_##FROM_TYPE##_fcmp_##NAME(                                \
      TYPE a, TYPE b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc) {                                                       \
    printf("%p, %s, %s\n", loc, "fcmp" #NAME, loc);                            \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_t ma, mb;                                                           \
      mpfr_init2(ma, significand);                                             \
      mpfr_init2(mb, significand);                                             \
      mpfr_set_##MPFR_GET(ma, a, ROUNDING_MODE);                               \
      mpfr_set_##MPFR_GET(mb, b, ROUNDING_MODE);                               \
      int ret = mpfr_cmp(ma, mb);                                              \
      mpfr_clear(ma);                                                          \
      mpfr_clear(mb);                                                          \
      return ret CMP;                                                          \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_double_to_ptr(a);                        \
      __raptor_fp *mb = __raptor_fprt_double_to_ptr(b);                        \
      int ret = mpfr_cmp(ma->result, mb->result);                              \
      return ret CMP;                                                          \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

__RAPTOR_MPFR_ORIGINAL_ATTRIBUTES
bool __raptor_fprt_original_64_52_intr_llvm_is_fpclass_f64(double a,
                                                           int32_t tests);
__RAPTOR_MPFR_ATTRIBUTES bool __raptor_fprt_64_52_intr_llvm_is_fpclass_f64(
    double a, int32_t tests, int64_t exponent, int64_t significand,
    int64_t mode, const char *loc) {
  return __raptor_fprt_original_64_52_intr_llvm_is_fpclass_f64(
      __raptor_fprt_64_52_get(a, exponent, significand, mode, loc), tests);
}

#define __RAPTOR_MPFR_LROUND(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, RET, ARG1,      \
                             MPFR_SET_ARG1, ROUNDING_MODE)                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc) {                                                       \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_t ma;                                                               \
      mpfr_init2(ma, significand);                                             \
      mpfr_set_##MPFR_SET_ARG1(ma, a, ROUNDING_MODE);                          \
      RET c = mpfr_get_si(ma, ROUNDING_MODE);                                  \
      mpfr_clear(ma);                                                          \
      return c;                                                                \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#include "flops.def"

#ifdef __cplusplus
}
#endif

#endif // #ifndef __RAPTOR_RUNTIME_RAPTOR_MPFR__
