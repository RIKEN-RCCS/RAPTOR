//===- fprt/mpfr - MPFR wrappers ---------------------------------------===//
//
//                             Enzyme Project
//
// Part of the Enzyme Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// If using this code in an academic setting, please cite the following:
// @incollection{enzymeNeurips,
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
#ifndef __ENZYME_RUNTIME_ENZYME_MPFR__
#define __ENZYME_RUNTIME_ENZYME_MPFR__

#include <iostream>
#include <mpfr.h>
#include <stdint.h>
#include <stdlib.h>
#include <map>
#include <set>
#include <string.h>

#include <mpi.h>

#include "fprt.h"

#define MAX_MPFR_OPERANDS 3

#ifdef __cplusplus
extern "C" {
#endif

// TODO s
//
// (for MPFR ver. 2.1)
//
// We need to set the range of the allowed exponent using `mpfr_set_emin` and
// `mpfr_set_emax`. (This means we can also play with whether the range is
// centered around 0 (1?) or somewhere else)
//
// (also these need to be mutex'ed as the exponent change is global in mpfr and
// not float-specific) ... (mpfr seems to have thread safe mode - check if it is
// enabled or if it is enabled by default)
//
// For that we need to do this check:
//   If the user changes the exponent range, it is her/his responsibility to
//   check that all current floating-point variables are in the new allowed
//   range (for example using mpfr_check_range), otherwise the subsequent
//   behavior will be undefined, in the sense of the ISO C standard.
//
// MPFR docs state the following:
//   Note: Overflow handling is still experimental and currently implemented
//   partially. If an overflow occurs internally at the wrong place, anything
//   can happen (crash, wrong results, etc).
//
// Which we would like to avoid somehow.
//
// MPFR also has this limitation that we need to address for accurate
// simulation:
//   [...] subnormal numbers are not implemented.
//
// TODO we need to provide f32 versions, and also instrument the
// truncation/expansion between f32/f64/etc

bool excl_trunc = false;

// typedef struct __enzyme_fp {
//   mpfr_t result;
// #ifdef ENZYME_FPRT_ENABLE_SHADOW_RESIDUALS
//   double excl_result;
//   double shadow;
// #endif
// } __enzyme_fp;

#ifdef ENZYME_FPRT_ENABLE_DUMPING
#define ENZYME_DUMP(X, OP_TYPE, LLVM_OP_NAME, TAG)                             \
  do {                                                                         \
    fprintf(stderr, #OP_TYPE " " #LLVM_OP_NAME " " TAG ": %p ", X);            \
    fprintf(stderr, "%f\n",                                                    \
            mpfr_get_d(X->result, __ENZYME_MPFR_DEFAULT_ROUNDING_MODE));       \
  } while (0)
#define ENZYME_DUMP_INPUT(X, OP_TYPE, LLVM_OP_NAME)                            \
  ENZYME_DUMP(X, OP_TYPE, LLVM_OP_NAME, "in")
#define ENZYME_DUMP_RESULT(X, OP_TYPE, LLVM_OP_NAME)                           \
  ENZYME_DUMP(X, OP_TYPE, LLVM_OP_NAME, "res")
#else
#define ENZYME_DUMP_INPUT(X, OP_TYPE, LLVM_OP_NAME)                            \
  do {                                                                         \
  } while (0)
#define ENZYME_DUMP_RESULT(X, OP_TYPE, LLVM_OP_NAME)                           \
  do {                                                                         \
  } while (0)
#endif

#ifdef ENZYME_FPRT_ENABLE_SHADOW_RESIDUALS
double __enzyme_fprt_64_52_abs_err(double a, double b) {
  return std::abs(a - b);
}
#endif

#ifdef ENZYME_FPRT_ENABLE_GARBAGE_COLLECTION

void enzyme_fprt_gc_dump_status();
double enzyme_fprt_gc_mark_seen(double a);
void enzyme_fprt_gc_doit();

__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_get(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch);

__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_new(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch);

__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_const(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc, mpfr_t *scratch);

__ENZYME_MPFR_ATTRIBUTES
__enzyme_fp *__enzyme_fprt_64_52_new_intermediate(int64_t exponent,
                                                  int64_t significand,
                                                  int64_t mode,
                                                  const char *loc);

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_delete(double a, int64_t exponent, int64_t significand,
                                int64_t mode, const char *loc, mpfr_t *scratch);

#else

__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_get(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
  __enzyme_fp *a = __enzyme_fprt_double_to_ptr(_a);
  return mpfr_get_d(a->result, __ENZYME_MPFR_DEFAULT_ROUNDING_MODE);
}

__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_new(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
  __enzyme_fp *a = (__enzyme_fp *)malloc(sizeof(__enzyme_fp));
  if (!a)
    exit(__ENZYME_MPFR_MALLOC_FAILURE_EXIT_STATUS);
  mpfr_init2(a->result, significand);
  mpfr_set_d(a->result, _a, __ENZYME_MPFR_DEFAULT_ROUNDING_MODE);
  return __enzyme_fprt_ptr_to_double(a);
}

__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_const(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc, mpfr_t *scratch) {
  // TODO This should really be called only once for an appearance in the code,
  // currently it is called every time a flop uses a constant.
  return __enzyme_fprt_64_52_new(_a, exponent, significand, mode, loc, scratch);
}

__ENZYME_MPFR_ATTRIBUTES
__enzyme_fp *__enzyme_fprt_64_52_new_intermediate(int64_t exponent,
                                                  int64_t significand,
                                                  int64_t mode,
                                                  const char *loc) {
  __enzyme_fp *a = (__enzyme_fp *)malloc(sizeof(__enzyme_fp));
  if (!a)
    exit(__ENZYME_MPFR_MALLOC_FAILURE_EXIT_STATUS);
  mpfr_init2(a->result, significand);
  return a;
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_delete(double a, int64_t exponent, int64_t significand,
                                int64_t mode, const char *loc,
                                mpfr_t *scratch) {
  free(__enzyme_fprt_double_to_ptr(a));
}

#endif

// Handle the case where people zero out memory and expect the floating
// point numbers there to be zero.
__ENZYME_MPFR_ATTRIBUTES
double __enzyme_fprt_64_52_check_zero(double _a, int64_t exponent,
                                      int64_t significand, int64_t mode,
                                      const char *loc, mpfr_t *scratch) {
  if ((*(uint64_t *)(&_a)) == 0)
    return __enzyme_fprt_64_52_const(0, exponent, significand, mode, loc,
                                     scratch);
  else
    return _a;
}

__ENZYME_MPFR_ATTRIBUTES
__enzyme_fp *__enzyme_fprt_double_to_ptr_checked(double d, int64_t exponent,
                                                 int64_t significand,
                                                 int64_t mode, const char *loc,
                                                 mpfr_t *scratch) {
  d = __enzyme_fprt_64_52_check_zero(d, exponent, significand, mode, loc,
                                     scratch);
  return __enzyme_fprt_double_to_ptr(d);
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_trunc_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_double_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_float_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_half_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_trunc_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_double_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_float_flop_count();

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_half_flop_count();

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch);

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_32_23_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch);

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_16_10_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch);

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_trunc_change(int64_t is_truncating,
                                int64_t to_e,
                                int64_t to_m,
                                int64_t mode);



__ENZYME_MPFR_ATTRIBUTES
void *__enzyme_fprt_64_52_get_scratch(int64_t to_e, int64_t to_m, int64_t mode,
                                      const char *loc, void *scratch);

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_free_scratch(int64_t to_e, int64_t to_m, int64_t mode,
                                      const char *loc, void *scratch);

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_reset_shadow_trace();

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_reset_shadow_trace();

typedef struct __enzyme_op {
  const char *op;             // Operation name
  double l1_err = 0;          // Running error.
  long long count_thresh = 0; // Number of error violations
  long long count = 0;        // Number of samples
  long long count_ignore = 0;
} __enzyme_op;

__ENZYME_MPFR_ATTRIBUTES
std::map<const char *, struct __enzyme_op> opdata;

__ENZYME_MPFR_ATTRIBUTES
void enzyme_fprt_op_dump_status(int num);

__ENZYME_MPFR_ATTRIBUTES
void enzyme_fprt_op_clear();

#ifdef ENZYME_FPRT_ENABLE_SHADOW_RESIDUALS
#define SHADOW_ERR_REL 6.25e-1   //
#define SHADOW_ERR_ABS 6.25e-1   // If reference is 0.
// #define SHADOW_ERR_REL 2.5e-3   //
// #define SHADOW_ERR_ABS 2.5e-3   // If reference is 0.
// #define SHADOW_ERR_REL 6.0e-8   //
// #define SHADOW_ERR_ABS 6.0e-8   // If reference is 0.


// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __ENZYME_MPFR_LROUND(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, RET, ARG1,      \
                             MPFR_SET_ARG1, ROUNDING_MODE)                     \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      RET c = mpfr_get_si(scratch[0], ROUNDING_MODE);                          \
      return c;                                                                \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __ENZYME_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __ENZYME_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a); \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], ROUNDING_MODE);            \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mc->shadow =                                                             \
          __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->shadow);                                                     \
      if (excl_trunc) {                                                        \
        trunc_excl_flop_counter.fetch_add(1, std::memory_order_relaxed);       \
        mc->excl_result =                                                      \
          __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ma->excl_result); \
        mpfr_set_##MPFR_SET_ARG1(mc->result, mc->excl_result, ROUNDING_MODE);  \
      } else {                                                                 \
        trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);            \
        mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, ROUNDING_MODE);          \
        mc->excl_result = mpfr_get_##MPFR_GET(mc->result, ROUNDING_MODE);      \
      }                                                                        \
      ENZYME_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      double trunc = mpfr_get_##MPFR_GET(mc->result,                           \
                                         __ENZYME_MPFR_DEFAULT_ROUNDING_MODE); \
      double err = __enzyme_fprt_64_52_abs_err(trunc, mc->shadow);             \
      if (!opdata[loc].count)                                                  \
        opdata[loc].op = #LLVM_OP_NAME;                                        \
      if (trunc != 0 && err / trunc > SHADOW_ERR_REL) {                        \
        ++opdata[loc].count_thresh;                                            \
      } else if (trunc == 0 && err > SHADOW_ERR_ABS) {                         \
        ++opdata[loc].count_thresh;                                            \
      }                                                                        \
      opdata[loc].l1_err += err;                                               \
      ++opdata[loc].count;                                                     \
      return __enzyme_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __ENZYME_MPFR_BIN_INT(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME,           \
                              FROM_TYPE, RET, MPFR_GET, ARG1, MPFR_SET_ARG1,   \
                              ARG2, ROUNDING_MODE)                             \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], b, ROUNDING_MODE);         \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, b, ROUNDING_MODE);         \
      ENZYME_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __enzyme_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __ENZYME_MPFR_BIN(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE,    \
                          RET, MPFR_GET, ARG1, MPFR_SET_ARG1, ARG2,            \
                          MPFR_SET_ARG2, ROUNDING_MODE)                        \
  __ENZYME_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a,  \
                                                                      ARG2 b); \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_set_##MPFR_SET_ARG2(scratch[1], b, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], scratch[1],                \
                            ROUNDING_MODE);                                    \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mb = __enzyme_fprt_double_to_ptr_checked(                   \
          b, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      ENZYME_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      mc->shadow =                                                             \
          __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->shadow, mb->shadow);                                         \
      if (excl_trunc) {                                                        \
        trunc_excl_flop_counter.fetch_add(1, std::memory_order_relaxed);       \
        mc->excl_result =                                                      \
          __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->excl_result, mb->excl_result);                               \
        mpfr_set_##MPFR_SET_ARG1(mc->result, mc->excl_result, ROUNDING_MODE);  \
      } else {                                                                 \
        trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);            \
        mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, mb->result,              \
                              ROUNDING_MODE);                                  \
        mc->excl_result = mpfr_get_##MPFR_GET(mc->result, ROUNDING_MODE);      \
      }                                                                        \
      ENZYME_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      double trunc = mpfr_get_##MPFR_GET(mc->result,                           \
                                         __ENZYME_MPFR_DEFAULT_ROUNDING_MODE); \
      double err = __enzyme_fprt_64_52_abs_err(trunc, mc->shadow);             \
      if (!opdata[loc].count)                                                  \
        opdata[loc].op = #LLVM_OP_NAME;                                        \
      if (trunc != 0 && err / trunc > SHADOW_ERR_REL) {                        \
        ++opdata[loc].count_thresh;                                            \
      } else if (trunc == 0 && err > SHADOW_ERR_ABS) {                         \
        ++opdata[loc].count_thresh;                                            \
      }                                                                        \
      opdata[loc].l1_err += err;                                               \
      ++opdata[loc].count;                                                     \
      return __enzyme_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __ENZYME_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,        \
                              LLVM_TYPE, ROUNDING_MODE)                        \
  __ENZYME_MPFR_ORIGINAL_ATTRIBUTES                                            \
  TYPE __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(        \
      TYPE a, TYPE b, TYPE c);                                                 \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  TYPE __enzyme_fprt_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(          \
      TYPE a, TYPE b, TYPE c, int64_t exponent, int64_t significand,           \
      int64_t mode, const char *loc, mpfr_t *scratch) {                        \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(2, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_TYPE(scratch[0], a, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[1], b, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[2], c, ROUNDING_MODE);                      \
      mpfr_mul(scratch[0], scratch[0], scratch[1], ROUNDING_MODE);             \
      mpfr_add(scratch[0], scratch[0], scratch[2], ROUNDING_MODE);             \
      TYPE res = mpfr_get_##MPFR_TYPE(scratch[0], ROUNDING_MODE);              \
      return res;                                                              \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mb = __enzyme_fprt_double_to_ptr_checked(                   \
          b, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_double_to_ptr_checked(                   \
          c, exponent, significand, mode, loc, scratch);                       \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      ENZYME_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      ENZYME_DUMP_INPUT(mc, OP_TYPE, LLVM_OP_NAME);                            \
      __enzyme_fp *madd = __enzyme_fprt_64_52_new_intermediate(                \
          exponent, significand, mode, loc);                                   \
      madd->shadow =                                                           \
          __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->shadow, mb->shadow, mc->shadow);                             \
      if (excl_trunc) {                                                        \
        trunc_excl_flop_counter.fetch_add(2, std::memory_order_relaxed);       \
        madd->shadow =                                                         \
            __enzyme_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(   \
                ma->shadow, mb->shadow, mc->shadow);                           \
        mpfr_set_##MPFR_TYPE(madd->result, madd->excl_result, ROUNDING_MODE);  \
      } else {                                                                 \
        trunc_flop_counter.fetch_add(2, std::memory_order_relaxed);            \
        mpfr_t mmul;                                                           \
        mpfr_init2(mmul, significand);                                         \
        mpfr_mul(madd->result, ma->result, mb->result, ROUNDING_MODE);         \
        mpfr_add(madd->result, madd->result, mc->result, ROUNDING_MODE);       \
        mpfr_clear(mmul);                                                      \
        madd->excl_result = mpfr_get_##MPFR_TYPE(madd->result, ROUNDING_MODE); \
      }                                                                        \
      ENZYME_DUMP_RESULT(__enzyme_fprt_double_to_ptr(madd), OP_TYPE,           \
                         LLVM_OP_NAME);                                        \
      double trunc = mpfr_get_##MPFR_TYPE(                                     \
          madd->result, __ENZYME_MPFR_DEFAULT_ROUNDING_MODE);                  \
      double err = __enzyme_fprt_64_52_abs_err(trunc, madd->shadow);           \
      if (!opdata[loc].count)                                                  \
        opdata[loc].op = #LLVM_OP_NAME;                                        \
      if (trunc != 0 && err / trunc > SHADOW_ERR_REL) {                        \
        ++opdata[loc].count_thresh;                                            \
      } else if (trunc == 0 && err > SHADOW_ERR_ABS) {                         \
        ++opdata[loc].count_thresh;                                            \
      }                                                                        \
      opdata[loc].l1_err += err;                                               \
      ++opdata[loc].count;                                                     \
      return __enzyme_fprt_ptr_to_double(madd);                                \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

// TODO This does not currently make distinctions between ordered/unordered.
#define __ENZYME_MPFR_FCMP_IMPL(NAME, ORDERED, CMP, FROM_TYPE, TYPE, MPFR_GET, \
                                ROUNDING_MODE)                                 \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  bool __enzyme_fprt_##FROM_TYPE##_fcmp_##NAME(                                \
      TYPE a, TYPE b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_GET(scratch[0], a, ROUNDING_MODE);                       \
      mpfr_set_##MPFR_GET(scratch[1], b, ROUNDING_MODE);                       \
      int ret = mpfr_cmp(scratch[0], scratch[1]);                              \
      return ret CMP;                                                          \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mb = __enzyme_fprt_double_to_ptr_checked(                   \
          b, exponent, significand, mode, loc, scratch);                       \
      int ret = mpfr_cmp(ma->result, mb->result);                              \
      return ret CMP;                                                          \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }
#else
// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __ENZYME_MPFR_LROUND(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, RET, ARG1,      \
                             MPFR_SET_ARG1, ROUNDING_MODE)                     \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      RET c = mpfr_get_si(scratch[0], ROUNDING_MODE);                          \
      return c;                                                                \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __ENZYME_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], ROUNDING_MODE);            \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, ROUNDING_MODE);            \
      ENZYME_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __enzyme_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __ENZYME_MPFR_BIN_INT(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME,           \
                              FROM_TYPE, RET, MPFR_GET, ARG1, MPFR_SET_ARG1,   \
                              ARG2, ROUNDING_MODE)                             \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], b, ROUNDING_MODE);         \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, b, ROUNDING_MODE);         \
      ENZYME_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __enzyme_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __ENZYME_MPFR_BIN(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE,    \
                          RET, MPFR_GET, ARG1, MPFR_SET_ARG1, ARG2,            \
                          MPFR_SET_ARG2, ROUNDING_MODE)                        \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  RET __enzyme_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_set_##MPFR_SET_ARG2(scratch[1], b, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], scratch[1],                \
                            ROUNDING_MODE);                                    \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mb = __enzyme_fprt_double_to_ptr_checked(                   \
          b, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_64_52_new_intermediate(                  \
          exponent, significand, mode, loc);                                   \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      ENZYME_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, mb->result,                \
                            ROUNDING_MODE);                                    \
      ENZYME_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __enzyme_fprt_ptr_to_double(mc);                                  \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __ENZYME_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,        \
                              LLVM_TYPE, ROUNDING_MODE)                        \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  TYPE __enzyme_fprt_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(          \
      TYPE a, TYPE b, TYPE c, int64_t exponent, int64_t significand,           \
      int64_t mode, const char *loc, mpfr_t *scratch) {                        \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter += 2;                                                 \
      mpfr_set_##MPFR_TYPE(scratch[0], a, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[1], b, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[2], c, ROUNDING_MODE);                      \
      mpfr_mul(scratch[0], scratch[0], scratch[1], ROUNDING_MODE);             \
      mpfr_add(scratch[0], scratch[0], scratch[2], ROUNDING_MODE);             \
      TYPE res = mpfr_get_##MPFR_TYPE(scratch[0], ROUNDING_MODE);              \
      return res;                                                              \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mb = __enzyme_fprt_double_to_ptr_checked(                   \
          b, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mc = __enzyme_fprt_double_to_ptr_checked(                   \
          c, exponent, significand, mode, loc, scratch);                       \
      ENZYME_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      ENZYME_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      ENZYME_DUMP_INPUT(mc, OP_TYPE, LLVM_OP_NAME);                            \
      double mmul = __enzyme_fprt_##FROM_TYPE##_binop_fmul(                    \
          __enzyme_fprt_ptr_to_double(ma), __enzyme_fprt_ptr_to_double(mb),    \
          exponent, significand, mode, loc, scratch);                          \
      double madd = __enzyme_fprt_##FROM_TYPE##_binop_fadd(                    \
          mmul, __enzyme_fprt_ptr_to_double(mc), exponent, significand, mode,  \
          loc, scratch);                                                       \
      ENZYME_DUMP_RESULT(__enzyme_fprt_double_to_ptr(madd), OP_TYPE,           \
                         LLVM_OP_NAME);                                        \
      return madd;                                                             \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

// TODO This does not currently make distinctions between ordered/unordered.
#define __ENZYME_MPFR_FCMP_IMPL(NAME, ORDERED, CMP, FROM_TYPE, TYPE, MPFR_GET, \
                                ROUNDING_MODE)                                 \
  __ENZYME_MPFR_ATTRIBUTES                                                     \
  bool __enzyme_fprt_##FROM_TYPE##_fcmp_##NAME(                                \
      TYPE a, TYPE b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__enzyme_fprt_is_op_mode(mode)) {                                      \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      mpfr_set_##MPFR_GET(scratch[0], a, ROUNDING_MODE);                       \
      mpfr_set_##MPFR_GET(scratch[1], b, ROUNDING_MODE);                       \
      int ret = mpfr_cmp(scratch[0], scratch[1]);                              \
      return ret CMP;                                                          \
    } else if (__enzyme_fprt_is_mem_mode(mode)) {                              \
      trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);              \
      __enzyme_fp *ma = __enzyme_fprt_double_to_ptr_checked(                   \
          a, exponent, significand, mode, loc, scratch);                       \
      __enzyme_fp *mb = __enzyme_fprt_double_to_ptr_checked(                   \
          b, exponent, significand, mode, loc, scratch);                       \
      int ret = mpfr_cmp(ma->result, mb->result);                              \
      return ret CMP;                                                          \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }
#endif  // ENZYME_FPRT_ENABLE_SHADOW_RESIDUALS

__ENZYME_MPFR_ORIGINAL_ATTRIBUTES
bool __enzyme_fprt_original_64_52_intr_llvm_is_fpclass_f64(double a,
                                                           int32_t tests);
__ENZYME_MPFR_ATTRIBUTES bool __enzyme_fprt_64_52_intr_llvm_is_fpclass_f64(
    double a, int32_t tests, int64_t exponent, int64_t significand,
    int64_t mode, const char *loc, mpfr_t *scratch) {
  return __enzyme_fprt_original_64_52_intr_llvm_is_fpclass_f64(
      __enzyme_fprt_64_52_get(a, exponent, significand, mode, loc, scratch),
      tests);
}

#include "flops.def"

#ifdef __cplusplus
}
#endif

#endif // #ifndef __ENZYME_RUNTIME_ENZYME_MPFR__
