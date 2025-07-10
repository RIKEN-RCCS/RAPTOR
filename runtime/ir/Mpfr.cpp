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

#include <map>
#include <mpfr.h>
#include <stdint.h>
#include <stdlib.h>

#include "raptor/Common.h"

// TODO s
//
// (for MPFR ver. 2.1)
//
// We set the range of the allowed exponent using `mpfr_set_emin` and
// `mpfr_set_emax`.
// But these need to be mutex'ed as the exponent change is global in mpfr and
// not float-specific ... (mpfr seems to have thread safe mode - check if it is
// enabled or if it is enabled by default)
//
// For that we need to do this check for mem mode:
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

// NOTE: MPFR_FP_EMULATION
// We need to add 1 to the mantissa width to get faithful fp emulation. See here
// https://www.mpfr.org/mpfr-3.1.4/mpfr.html#index-mpfr_005fsubnormalize
// and here
// https://stackoverflow.com/questions/38664778/subnormal-numbers-in-different-precisions-with-mpfr

#ifdef RAPTOR_FPRT_ENABLE_DUMPING
#define RAPTOR_DUMP(X, OP_TYPE, LLVM_OP_NAME, TAG)                             \
  do {                                                                         \
    fprintf(stderr, #OP_TYPE " " #LLVM_OP_NAME " " TAG ": %p ", X);            \
    fprintf(stderr, "%f\n",                                                    \
            mpfr_get_d(X->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE));       \
  } while (0)
#define RAPTOR_DUMP_INPUT(X, OP_TYPE, LLVM_OP_NAME)                            \
  RAPTOR_DUMP(X, OP_TYPE, LLVM_OP_NAME, "in")
#define RAPTOR_DUMP_RESULT(X, OP_TYPE, LLVM_OP_NAME)                           \
  RAPTOR_DUMP(X, OP_TYPE, LLVM_OP_NAME, "res")
#else
#define RAPTOR_DUMP_INPUT(X, OP_TYPE, LLVM_OP_NAME)                            \
  do {                                                                         \
  } while (0)
#define RAPTOR_DUMP_RESULT(X, OP_TYPE, LLVM_OP_NAME)                           \
  do {                                                                         \
  } while (0)
#endif

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_trunc_change(int64_t is_push, int64_t to_e, int64_t to_m,
                                int64_t mode, const char *loc, void *scratch) {
  if (global_is_truncating && is_push &&
      !__raptor_fprt_is_full_module_op_mode(mode)) {
    puts("Nested truncation is unsupported");
    abort();
  }
  global_is_truncating.store(is_push);

  // If we are starting to truncate, set the max and min exponents
  // Can't do it for mem mode currently because we may have truncated variables
  // with unsupported exponent lengths, and those would result in undefined
  // behaviour.
  if (is_push && __raptor_fprt_is_op_mode(mode)) {
    // TODO we need a stack if we want to support nested truncations
    // see MPFR_FP_EMULATION
    int64_t max_e = 1 << (to_e - 1);
    int64_t min_e = -max_e + 2 - to_m + 2;
    // TODO currently in full module truncation mode we assume that all of the
    // exponents we truncate to are the same. Otherwise we need to have a stack
    // which we pop and restore previous values.
    mpfr_set_emax(max_e);
    mpfr_set_emin(min_e);
  }
}

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_abs_err(CPP_TY a, CPP_TY b) {               \
    return std::abs(a - b);                                                    \
  }                                                                            \
                                                                               \
  /* Handle the case where people zero out memory and expect the floating */   \
  /* point numbers there to be zero. */                                        \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_check_zero(                                 \
      CPP_TY _a, int64_t exponent, int64_t significand, int64_t mode,          \
      const char *loc, mpfr_t *scratch) {                                      \
    if constexpr (sizeof(void *) == sizeof(CPP_TY)) {                          \
      if (checked_raptor_bitcast<uint64_t>(_a) == 0)                           \
        return __raptor_fprt_##FROM_TY##_const(0, exponent, significand, mode, \
                                               loc, scratch);                  \
      else                                                                     \
        return _a;                                                             \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  __raptor_fp *__raptor_fprt_##FROM_TY##_to_ptr_checked(                       \
      CPP_TY d, int64_t exponent, int64_t significand, int64_t mode,           \
      const char *loc, mpfr_t *scratch) {                                      \
    d = __raptor_fprt_##FROM_TY##_check_zero(d, exponent, significand, mode,   \
                                             loc, scratch);                    \
    return __raptor_fprt_##FROM_TY##_to_ptr(d);                                \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_trunc_change(                                 \
      int64_t is_push, int64_t to_e, int64_t to_m, int64_t mode,               \
      const char *loc, void *scratch) {                                        \
    __raptor_fprt_trunc_change(is_push, to_e, to_m, mode, loc, scratch);       \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void *__raptor_fprt_##FROM_TY##_get_scratch(int64_t to_e, int64_t to_m,      \
                                              int64_t mode, const char *loc,   \
                                              void *scratch) {                 \
    mpfr_t *mem = (mpfr_t *)malloc(sizeof(mem[0]) * MAX_MPFR_OPERANDS);        \
    for (unsigned i = 0; i < MAX_MPFR_OPERANDS; i++)                           \
      mpfr_init2(mem[i], to_m + 1); /* see MPFR_FP_EMULATION */                \
    return mem;                                                                \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_free_scratch(int64_t to_e, int64_t to_m,      \
                                              int64_t mode, const char *loc,   \
                                              void *scratch) {                 \
    mpfr_t *mem = (mpfr_t *)scratch;                                           \
    for (unsigned i = 0; i < MAX_MPFR_OPERANDS; i++)                           \
      mpfr_clear(mem[i]);                                                      \
    free(mem);                                                                 \
  }

#include "raptor/FloatTypes.def"
#undef RAPTOR_FLOAT_TYPE

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_trunc_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_64_count(int64_t exponent, int64_t significand,
                                 int64_t mode, const char *loc,
                                 mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_32_count(int64_t exponent, int64_t significand,
                                 int64_t mode, const char *loc,
                                 mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_16_count(int64_t exponent, int64_t significand,
                                 int64_t mode, const char *loc,
                                 mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_trunc_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_double_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_float_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_half_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_trunc_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_double_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_float_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_half_flop_count();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_trunc_store();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_trunc_load();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_original_store();

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_original_load();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_trunc_store();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_trunc_load();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_original_store();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_original_load();

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_memory_access(void *, int64_t size, int64_t is_store);

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_64_count(int64_t exponent, int64_t significand,
                                 int64_t mode, const char *loc,
                                 mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_32_count(int64_t exponent, int64_t significand,
                                 int64_t mode, const char *loc,
                                 mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_16_count(int64_t exponent, int64_t significand,
                                 int64_t mode, const char *loc,
                                 mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_reset_shadow_trace();

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_reset_shadow_trace();

std::map<const char *, struct __raptor_op> opdata;

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_op_dump_status(int num);

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_op_clear();

#ifdef RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS
// #define SHADOW_ERR_REL 6.25e-1   //
// #define SHADOW_ERR_ABS 6.25e-1   // If reference is 0.
#define SHADOW_ERR_REL 2.5e-4 // 12bit
#define SHADOW_ERR_ABS 2.5e-4 // If reference is 0.
// #define SHADOW_ERR_REL 6.0e-8   //
// #define SHADOW_ERR_ABS 6.0e-8   // If reference is 0.

// TODO this is a bit sketchy if the user cast their float to int before calling
// this. We need to detect these patterns
#define __RAPTOR_MPFR_LROUND(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, RET, ARG1,      \
                             MPFR_SET_ARG1, ROUNDING_MODE)                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      RET c = mpfr_get_si(scratch[0], ROUNDING_MODE);                          \
      return c;                                                                \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a); \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], ROUNDING_MODE);            \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_new_intermediate(          \
          exponent, significand, mode, loc, scratch);                          \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mc->shadow =                                                             \
          __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->shadow);                                                     \
      if (excl_trunc) {                                                        \
        __raptor_fprt_##FROM_TYPE##_count(exponent, significand, mode, loc,    \
                                          scratch);                            \
        mc->excl_result =                                                      \
            __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(   \
                ma->excl_result);                                              \
        mpfr_set_##MPFR_SET_ARG1(mc->result, mc->excl_result, ROUNDING_MODE);  \
      } else {                                                                 \
        __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);  \
        mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, ROUNDING_MODE);          \
        mc->excl_result = mpfr_get_##MPFR_GET(mc->result, ROUNDING_MODE);      \
      }                                                                        \
      RAPTOR_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      double trunc = mpfr_get_##MPFR_GET(mc->result,                           \
                                         __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE); \
      double err = __raptor_fprt_##FROM_TYPE##_abs_err(trunc, mc->shadow);     \
      if (!opdata[loc].count)                                                  \
        opdata[loc].op = #LLVM_OP_NAME;                                        \
      if (trunc != 0 && err / trunc > SHADOW_ERR_REL) {                        \
        ++opdata[loc].count_thresh;                                            \
      } else if (trunc == 0 && err > SHADOW_ERR_ABS) {                         \
        ++opdata[loc].count_thresh;                                            \
      }                                                                        \
      opdata[loc].l1_err += err;                                               \
      ++opdata[loc].count;                                                     \
      return __raptor_fprt_ptr_to_##FROM_TYPE(mc);                             \
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
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], b, ROUNDING_MODE);         \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_new_intermediate(          \
          exponent, significand, mode, loc, scratch);                          \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, b, ROUNDING_MODE);         \
      mc->excl_result = mpfr_get_##MPFR_GET(mc->result, ROUNDING_MODE);        \
      RAPTOR_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __raptor_fprt_ptr_to_##FROM_TYPE(mc);                             \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_BIN(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE,    \
                          RET, MPFR_GET, ARG1, MPFR_SET_ARG1, ARG2,            \
                          MPFR_SET_ARG2, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(ARG1 a,  \
                                                                      ARG2 b); \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, ARG2 b, int64_t exponent, int64_t significand, int64_t mode,     \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_set_##MPFR_SET_ARG2(scratch[1], b, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], scratch[1],                \
                            ROUNDING_MODE);                                    \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mb = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          b, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_new_intermediate(          \
          exponent, significand, mode, loc, scratch);                          \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      RAPTOR_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      mc->shadow =                                                             \
          __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->shadow, mb->shadow);                                         \
      if (excl_trunc) {                                                        \
        __raptor_fprt_##FROM_TYPE##_count(exponent, significand, mode, loc,    \
                                          scratch);                            \
        mc->excl_result =                                                      \
            __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(   \
                ma->excl_result, mb->excl_result);                             \
        mpfr_set_##MPFR_SET_ARG1(mc->result, mc->excl_result, ROUNDING_MODE);  \
      } else {                                                                 \
        __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);  \
        mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, mb->result,              \
                              ROUNDING_MODE);                                  \
        mc->excl_result = mpfr_get_##MPFR_GET(mc->result, ROUNDING_MODE);      \
      }                                                                        \
      RAPTOR_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      double trunc = mpfr_get_##MPFR_GET(mc->result,                           \
                                         __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE); \
      double err = __raptor_fprt_##FROM_TYPE##_abs_err(trunc, mc->shadow);     \
      if (!opdata[loc].count)                                                  \
        opdata[loc].op = #LLVM_OP_NAME;                                        \
      if (trunc != 0 && err / trunc > SHADOW_ERR_REL) {                        \
        ++opdata[loc].count_thresh;                                            \
      } else if (trunc == 0 && err > SHADOW_ERR_ABS) {                         \
        ++opdata[loc].count_thresh;                                            \
      }                                                                        \
      opdata[loc].l1_err += err;                                               \
      ++opdata[loc].count;                                                     \
      return __raptor_fprt_ptr_to_##FROM_TYPE(mc);                             \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,        \
                              LLVM_TYPE, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  TYPE __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(        \
      TYPE a, TYPE b, TYPE c);                                                 \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  TYPE __raptor_fprt_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(          \
      TYPE a, TYPE b, TYPE c, int64_t exponent, int64_t significand,           \
      int64_t mode, const char *loc, mpfr_t *scratch) {                        \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_TYPE(scratch[0], a, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[1], b, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[2], c, ROUNDING_MODE);                      \
      mpfr_mul(scratch[0], scratch[0], scratch[1], ROUNDING_MODE);             \
      mpfr_add(scratch[0], scratch[0], scratch[2], ROUNDING_MODE);             \
      TYPE res = mpfr_get_##MPFR_TYPE(scratch[0], ROUNDING_MODE);              \
      return res;                                                              \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mb = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          b, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          c, exponent, significand, mode, loc, scratch);                       \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      RAPTOR_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      RAPTOR_DUMP_INPUT(mc, OP_TYPE, LLVM_OP_NAME);                            \
      __raptor_fp *madd = __raptor_fprt_##FROM_TYPE##_new_intermediate(        \
          exponent, significand, mode, loc, scratch);                          \
      madd->shadow =                                                           \
          __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
              ma->shadow, mb->shadow, mc->shadow);                             \
      if (excl_trunc) {                                                        \
        __raptor_fprt_##FROM_TYPE##_count(exponent, significand, mode, loc,    \
                                          scratch);                            \
        madd->excl_result =                                                    \
            __raptor_fprt_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(   \
                ma->excl_result, mb->excl_result, mc->excl_result);            \
        mpfr_set_##MPFR_TYPE(madd->result, madd->excl_result, ROUNDING_MODE);  \
      } else {                                                                 \
        __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);  \
        mpfr_t mmul;                                                           \
        mpfr_init2(mmul, significand + 1); /* see MPFR_FP_EMULATION */         \
        mpfr_mul(madd->result, ma->result, mb->result, ROUNDING_MODE);         \
        mpfr_add(madd->result, madd->result, mc->result, ROUNDING_MODE);       \
        mpfr_clear(mmul);                                                      \
        madd->excl_result = mpfr_get_##MPFR_TYPE(madd->result, ROUNDING_MODE); \
      }                                                                        \
      RAPTOR_DUMP_RESULT(__raptor_fprt_##FROM_TYPE##_to_ptr(madd), OP_TYPE,    \
                         LLVM_OP_NAME);                                        \
      double trunc = mpfr_get_##MPFR_TYPE(                                     \
          madd->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);                  \
      double err = __raptor_fprt_##FROM_TYPE##_abs_err(trunc, madd->shadow);   \
      if (!opdata[loc].count)                                                  \
        opdata[loc].op = #LLVM_OP_NAME;                                        \
      if (trunc != 0 && err / trunc > SHADOW_ERR_REL) {                        \
        ++opdata[loc].count_thresh;                                            \
      } else if (trunc == 0 && err > SHADOW_ERR_ABS) {                         \
        ++opdata[loc].count_thresh;                                            \
      }                                                                        \
      opdata[loc].l1_err += err;                                               \
      ++opdata[loc].count;                                                     \
      return __raptor_fprt_ptr_to_##FROM_TYPE(madd);                           \
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
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_GET(scratch[0], a, ROUNDING_MODE);                       \
      mpfr_set_##MPFR_GET(scratch[1], b, ROUNDING_MODE);                       \
      int ret = mpfr_cmp(scratch[0], scratch[1]);                              \
      return ret CMP;                                                          \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mb = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
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
#define __RAPTOR_MPFR_LROUND(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, RET, ARG1,      \
                             MPFR_SET_ARG1, ROUNDING_MODE)                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      RET c = mpfr_get_si(scratch[0], ROUNDING_MODE);                          \
      return c;                                                                \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprt_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(                  \
      ARG1 a, int64_t exponent, int64_t significand, int64_t mode,             \
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], ROUNDING_MODE);            \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_new_intermediate(          \
          exponent, significand, mode, loc, scratch);                          \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, ROUNDING_MODE);            \
      RAPTOR_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __raptor_fprt_ptr_to_##FROM_TYPE(mc);                             \
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
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], b, ROUNDING_MODE);         \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_new_intermediate(          \
          exponent, significand, mode, loc, scratch);                          \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, b, ROUNDING_MODE);         \
      RAPTOR_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __raptor_fprt_ptr_to_##FROM_TYPE(mc);                             \
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
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_SET_ARG1(scratch[0], a, ROUNDING_MODE);                  \
      mpfr_set_##MPFR_SET_ARG2(scratch[1], b, ROUNDING_MODE);                  \
      mpfr_##MPFR_FUNC_NAME(scratch[2], scratch[0], scratch[1],                \
                            ROUNDING_MODE);                                    \
      RET c = mpfr_get_##MPFR_GET(scratch[2], ROUNDING_MODE);                  \
      return c;                                                                \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mb = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          b, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_new_intermediate(          \
          exponent, significand, mode, loc, scratch);                          \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      RAPTOR_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      mpfr_##MPFR_FUNC_NAME(mc->result, ma->result, mb->result,                \
                            ROUNDING_MODE);                                    \
      RAPTOR_DUMP_RESULT(mc, OP_TYPE, LLVM_OP_NAME);                           \
      return __raptor_fprt_ptr_to_##FROM_TYPE(mc);                             \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define __RAPTOR_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,        \
                              LLVM_TYPE, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  TYPE __raptor_fprt_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(          \
      TYPE a, TYPE b, TYPE c, int64_t exponent, int64_t significand,           \
      int64_t mode, const char *loc, mpfr_t *scratch) {                        \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_TYPE(scratch[0], a, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[1], b, ROUNDING_MODE);                      \
      mpfr_set_##MPFR_TYPE(scratch[2], c, ROUNDING_MODE);                      \
      mpfr_mul(scratch[0], scratch[0], scratch[1], ROUNDING_MODE);             \
      mpfr_add(scratch[0], scratch[0], scratch[2], ROUNDING_MODE);             \
      TYPE res = mpfr_get_##MPFR_TYPE(scratch[0], ROUNDING_MODE);              \
      return res;                                                              \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mb = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          b, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mc = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          c, exponent, significand, mode, loc, scratch);                       \
      RAPTOR_DUMP_INPUT(ma, OP_TYPE, LLVM_OP_NAME);                            \
      RAPTOR_DUMP_INPUT(mb, OP_TYPE, LLVM_OP_NAME);                            \
      RAPTOR_DUMP_INPUT(mc, OP_TYPE, LLVM_OP_NAME);                            \
      double mmul = __raptor_fprt_##FROM_TYPE##_binop_fmul(                    \
          __raptor_fprt_ptr_to_##FROM_TYPE(ma),                                \
          __raptor_fprt_ptr_to_##FROM_TYPE(mb), exponent, significand, mode,   \
          loc, scratch);                                                       \
      double madd = __raptor_fprt_##FROM_TYPE##_binop_fadd(                    \
          mmul, __raptor_fprt_ptr_to_##FROM_TYPE(mc), exponent, significand,   \
          mode, loc, scratch);                                                 \
      RAPTOR_DUMP_RESULT(__raptor_fprt_##FROM_TYPE##_to_ptr(madd), OP_TYPE,    \
                         LLVM_OP_NAME);                                        \
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
      const char *loc, mpfr_t *scratch) {                                      \
    if (__raptor_fprt_is_op_mode(mode)) {                                      \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      mpfr_set_##MPFR_GET(scratch[0], a, ROUNDING_MODE);                       \
      mpfr_set_##MPFR_GET(scratch[1], b, ROUNDING_MODE);                       \
      int ret = mpfr_cmp(scratch[0], scratch[1]);                              \
      return ret CMP;                                                          \
    } else if (__raptor_fprt_is_mem_mode(mode)) {                              \
      __raptor_fprt_trunc_count(exponent, significand, mode, loc, scratch);    \
      __raptor_fp *ma = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          a, exponent, significand, mode, loc, scratch);                       \
      __raptor_fp *mb = __raptor_fprt_##FROM_TYPE##_to_ptr_checked(            \
          b, exponent, significand, mode, loc, scratch);                       \
      int ret = mpfr_cmp(ma->result, mb->result);                              \
      return ret CMP;                                                          \
    } else {                                                                   \
      abort();                                                                 \
    }                                                                          \
  }
#endif // RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS

#define __RAPTOR_MPFR_ISCLASS(FROM_TYPE, TYPE, LLVM_TYPE)                         \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES bool                                          \
      __raptor_fprt_original_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE(      \
          TYPE a, int32_t tests);                                                 \
  __RAPTOR_MPFR_ATTRIBUTES bool                                                   \
      __raptor_fprt_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE(               \
          TYPE a, int32_t tests, int64_t exponent, int64_t significand,           \
          int64_t mode, const char *loc, mpfr_t *scratch) {                       \
    return __raptor_fprt_original_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE( \
        __raptor_fprt_ieee_64_get(a, exponent, significand, mode, loc,            \
                                  scratch),                                       \
        tests);                                                                   \
  }

#include "Flops.def"
