//===- Trace.cpp - FLOP Garbage collection wrappers
//---------------------------------===//
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
// This file contains infrastructure for flop tracing
//
// It is implemented as a .cpp file and not as a header becaues we want to use
// C++ features and still be able to use it in C code.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <list>
#include <mpfr.h>
#include <stdint.h>
#include <stdlib.h>

#define RAPTOR_FPRT_ENABLE_GARBAGE_COLLECTION
#define RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS

#include <raptor/Common.h>
#include <raptor/raptor.h>

bool excl_trunc = false;

struct GCFloatTy {
  __raptor_fp fp;
  bool seen;
  GCFloatTy() : seen(false) {}
  ~GCFloatTy() {}
};
struct {
  std::list<GCFloatTy> all;
  void clear() { all.clear(); }

} __raptor_mpfr_fps;

// __raptor_mpfr_##FROM_TY##_id_to_fp vectors need to skip the first element
// because id == 0 is special case for __raptor_fprt_##FROM_TY##_check_zero
#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  std::vector<__raptor_fp *> __raptor_mpfr_##FROM_TY##_id_to_fp(1);            \
  std::vector<size_t> __raptor_mpfr_##FROM_TY##_free_id;
#include "raptor/FloatTypes.def"

template <typename CPP_TY>
inline std::enable_if_t<
  std::is_floating_point_v<CPP_TY> && sizeof(CPP_TY) == sizeof(uint32_t), 
  uint32_t> raptor_fp_id_fp_to_uint (CPP_TY f) 
{
  // Enable if already ensures that the type size match, no need to check again
  return raptor_bitcast<uint32_t>(f);
}

template <typename CPP_TY>
inline std::enable_if_t<
  std::is_floating_point_v<CPP_TY> && sizeof(CPP_TY) == sizeof(uint16_t), 
  uint16_t> raptor_fp_id_fp_to_uint (CPP_TY h) 
{
  // Enable if already ensures that the type size match, no need to check again
  return raptor_bitcast<uint16_t>(h);
}


template <typename CPP_TY>
inline std::enable_if_t<
  std::is_floating_point_v<CPP_TY> && sizeof(CPP_TY) == sizeof(__raptor_fp *), 
  CPP_TY> get_id_from_raptor_fp (__raptor_fp *p) 
{
  // Enable if already ensures that the type size match, no need to check again
  return raptor_bitcast<CPP_TY>(p);
}

template <typename CPP_TY>
inline std::enable_if_t<
  std::is_floating_point_v<CPP_TY> && sizeof(CPP_TY) == sizeof(uint32_t), 
  CPP_TY> get_id_from_raptor_fp (__raptor_fp *p) 
{
  // Enable if already ensures that the type size match, no need to check again
  return raptor_bitcast<CPP_TY>(p->id.f);
}

template <typename CPP_TY>
inline std::enable_if_t<
  std::is_floating_point_v<CPP_TY> && sizeof(CPP_TY) == sizeof(uint16_t), 
  CPP_TY> get_id_from_raptor_fp (__raptor_fp *p) 
{
  // Enable if already ensures that the type size match, no need to check again
  return raptor_bitcast<CPP_TY>(p->id.h);
}

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  template <typename T>                                                        \
  inline __raptor_fp * get_raptor_fp_from_##FROM_TY##_(T d) {                  \
    if constexpr (sizeof(T) == sizeof(__raptor_fp *))                          \
      return raptor_bitcast<__raptor_fp *>(d); /* type size already checked */ \
    else                                                                       \
      return __raptor_mpfr_##FROM_TY##_id_to_fp.at(raptor_fp_id_fp_to_uint(d));\
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  __raptor_fp * get_raptor_fp_from_##FROM_TY(CPP_TY d) {                       \
    return get_raptor_fp_from_##FROM_TY##_(d);                                 \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY get_##FROM_TY##_from_raptor_fp(__raptor_fp *p) {                      \
    return get_id_from_raptor_fp<CPP_TY>(p);                                   \
  }
#include "raptor/FloatTypes.def"

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_get(CPP_TY _a, int64_t exponent,            \
                                       int64_t significand, int64_t mode,      \
                                       const char *loc, void *scratch) {       \
    __raptor_fp *a = __raptor_fprt_##FROM_TY##_to_ptr(_a);                     \
    return mpfr_get_d(a->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);         \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_new(CPP_TY _a, int64_t exponent,            \
                                       int64_t significand, int64_t mode,      \
                                       const char *loc, void *scratch) {       \
    __raptor_mpfr_fps.all.push_back({});                                       \
    __raptor_fp *a = &__raptor_mpfr_fps.all.back().fp;                         \
    mpfr_init2(a->result, significand + 1); /* see MPFR_FP_EMULATION */        \
    mpfr_set_d(a->result, _a, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);            \
    a->excl_result = _a;                                                       \
    a->shadow = _a;                                                            \
    if constexpr (sizeof(CPP_TY) != sizeof(__raptor_fp *)) {                   \
      if (__raptor_mpfr_##FROM_TY##_free_id.empty()) {                         \
        a->id.d = __raptor_mpfr_##FROM_TY##_id_to_fp.size();                   \
        __raptor_mpfr_##FROM_TY##_id_to_fp.push_back(a);                       \
      } else {                                                                 \
        a->id.d = __raptor_mpfr_##FROM_TY##_free_id.back();                    \
        __raptor_mpfr_##FROM_TY##_free_id.pop_back();                          \
        __raptor_mpfr_##FROM_TY##_id_to_fp.at(a->id.d) = a;                    \
      }                                                                        \
    }                                                                          \
    return __raptor_fprt_ptr_to_##FROM_TY(a);                                  \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_const(CPP_TY _a, int64_t exponent,          \
                                         int64_t significand, int64_t mode,    \
                                         const char *loc, void *scratch) {     \
    /* TODO This should really be called only once for an appearance in the    \
     * code, currently it is called every time a flop uses a constant. */      \
    return __raptor_fprt_##FROM_TY##_new(_a, exponent, significand, mode, loc, \
                                         scratch);                             \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  __raptor_fp *__raptor_fprt_##FROM_TY##_new_intermediate(                     \
      int64_t exponent, int64_t significand, int64_t mode, const char *loc,    \
      void *scratch) {                                                         \
    __raptor_mpfr_fps.all.push_back({});                                       \
    __raptor_fp *a = &__raptor_mpfr_fps.all.back().fp;                         \
    if constexpr (sizeof(CPP_TY) != sizeof(__raptor_fp *)) {                   \
      if (__raptor_mpfr_##FROM_TY##_free_id.empty()) {                         \
        a->id.d = __raptor_mpfr_##FROM_TY##_id_to_fp.size();                   \
        __raptor_mpfr_##FROM_TY##_id_to_fp.push_back(a);                       \
      } else {                                                                 \
        a->id.d = __raptor_mpfr_##FROM_TY##_free_id.back();                    \
        __raptor_mpfr_##FROM_TY##_free_id.pop_back();                          \
        __raptor_mpfr_##FROM_TY##_id_to_fp.at(a->id.d) = a;                    \
      }                                                                        \
    }                                                                          \
    mpfr_init2(a->result, significand + 1); /* see MPFR_FP_EMULATION */        \
    return a;                                                                  \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_delete(CPP_TY a, int64_t exponent,            \
                                        int64_t significand, int64_t mode,     \
                                        const char *loc, void *scratch) {      \
    /* ignore for now */                                                       \
  }
#include "raptor/FloatTypes.def"

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_dump_status() {
  std::cerr << "Currently " << __raptor_mpfr_fps.all.size()
            << " floats allocated." << std::endl;
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_clear_seen() {
  for (auto &gcfp : __raptor_mpfr_fps.all)
    gcfp.seen = false;
}

__RAPTOR_MPFR_ATTRIBUTES
double raptor_fprt_gc_mark_seen(double a) {
  __raptor_fp *fp = __raptor_fprt_ieee_64_to_ptr(a);
  if (!fp)
    return a;
  intptr_t offset = (char *)&(((GCFloatTy *)nullptr)->fp) - (char *)nullptr;
  GCFloatTy *gcfp = (GCFloatTy *)((char *)fp - offset);
  gcfp->seen = true;
  return a;
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_doit() {
  for (auto it = __raptor_mpfr_fps.all.begin();
       it != __raptor_mpfr_fps.all.end();) {
    if (!it->seen) {
      mpfr_clear(it->fp.result);
      it = __raptor_mpfr_fps.all.erase(it);
    } else {
      ++it;
    }
  }
  raptor_fprt_gc_clear_seen();
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_start() { excl_trunc = true; }

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_end() { excl_trunc = false; }
