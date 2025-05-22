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
#include <llvm/Support/ErrorHandling.h>
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

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_64_52_get(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  __raptor_fp *a = __raptor_fprt_double_to_ptr(_a);
  return mpfr_get_d(a->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);
}

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_64_52_new(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  __raptor_mpfr_fps.all.push_back({});
  __raptor_fp *a = &__raptor_mpfr_fps.all.back().fp;
  mpfr_init2(a->result, significand);
  mpfr_set_d(a->result, _a, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);
#ifdef RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS
  a->excl_result = _a;
  a->shadow = _a;
#endif
  return __raptor_fprt_ptr_to_double(a);
}

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_64_52_const(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc) {
  // TODO This should really be called only once for an appearance in the code,
  // currently it is called every time a flop uses a constant.
  return __raptor_fprt_64_52_new(_a, exponent, significand, mode, loc);
}

__RAPTOR_MPFR_ATTRIBUTES
__raptor_fp *__raptor_fprt_64_52_new_intermediate(int64_t exponent,
                                                  int64_t significand,
                                                  int64_t mode,
                                                  const char *loc) {
  __raptor_mpfr_fps.all.push_back({});
  __raptor_fp *a = &__raptor_mpfr_fps.all.back().fp;
  mpfr_init2(a->result, significand);
  return a;
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_64_52_delete(double a, int64_t exponent, int64_t significand,
                                int64_t mode, const char *loc) {
  // ignore for now
}

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
  __raptor_fp *fp = __raptor_fprt_double_to_ptr(a);
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
void raptor_fprt_excl_trunc_start() {
  excl_trunc = true;
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_end() {
  excl_trunc = false;
}
