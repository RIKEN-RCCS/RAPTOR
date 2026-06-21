
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <ostream>
#include <utility>
#include <vector>

#include "raptor/Common.h"
#include "raptor/raptor.h"

// Global variable to count truncated flops
// TODO only implemented for op mode at the moment
std::atomic<long long> trunc_flop_counter = 0;
std::atomic<long long> double_flop_counter = 0;
std::atomic<long long> float_flop_counter = 0;
std::atomic<long long> half_flop_counter = 0;

std::atomic<long long> trunc_load_counter = 0;
std::atomic<long long> trunc_store_counter = 0;
std::atomic<long long> original_load_counter = 0;
std::atomic<long long> original_store_counter = 0;

extern std::map<const char *, struct __raptor_op> opdata;

// TODO this needs to be thread local
std::atomic<bool> global_is_truncating = false;

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_trunc_flop_count() { return trunc_flop_counter; }

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_double_flop_count() { return double_flop_counter; }

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_float_flop_count() { return float_flop_counter; }

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_half_flop_count() { return half_flop_counter; }

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_trunc_flop_count() {
  return __raptor_get_trunc_flop_count();
}

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_double_flop_count() {
  return __raptor_get_double_flop_count();
}

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_float_flop_count() {
  return __raptor_get_float_flop_count();
}

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_half_flop_count() {
  return __raptor_get_half_flop_count();
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_trunc_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
#ifndef RAPTOR_FPRT_DISABLE_TRUNC_FLOP_COUNT
  trunc_flop_counter.fetch_add(1, std::memory_order_relaxed);
#endif
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_64_count() {
  double_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_32_count() {
  float_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_16_count() {
  half_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_reset_shadow_trace() {
  long long ret = shadow_err_counter;
  shadow_err_counter = 0;
  return ret;
}

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_reset_shadow_trace() {
  return __raptor_reset_shadow_trace();
}

bool __op_dump_cmp(std::pair<const char *, __raptor_op> &a,
                   std::pair<const char *, __raptor_op> &b) {
  return a.second.count_thresh > b.second.count_thresh;
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_op_dump_status(unsigned num) {

  if (opdata.size() < num)
    num = opdata.size();

  std::cerr << "Information about top " << num << " operations." << std::endl;

  std::vector<std::pair<const char *, struct __raptor_op>> od_vec;
  std::vector<double> l1_vec;
  std::vector<long long> ct_vec, c_vec;

  // The order of iteration over keys will be the same on all processes.
  for (auto &it : opdata) {
    od_vec.push_back(it);
    l1_vec.push_back(it.second.l1_err);
    ct_vec.push_back(it.second.count_thresh);
    c_vec.push_back(it.second.count);
  }

  std::sort(od_vec.begin(), od_vec.end(), __op_dump_cmp);

  auto end = od_vec.begin() + num;
  for (auto it = od_vec.begin(); it != end; ++it) {
    std::cout << it->first << ": " << it->second.count << "x" << it->second.op
              << " L1 Error Norm: " << it->second.l1_err
              << " Number of violations: " << it->second.count_thresh
              << " Ignored " << it->second.count_ignore << " times."
              << std::endl;
  }
}

__RAPTOR_MPFR_ATTRIBUTES
void f_raptor_fprt_op_dump_status(unsigned num) {
  return __raptor_fprt_op_dump_status(num);
}

long long __raptor_get_memory_access_trunc_store() {
  return trunc_store_counter;
}
__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_trunc_load() { return trunc_load_counter; }

__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_original_store() {
  return original_store_counter;
}
__RAPTOR_MPFR_ATTRIBUTES
long long __raptor_get_memory_access_original_load() {
  return original_load_counter;
}

__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_trunc_store() {
  return __raptor_get_memory_access_trunc_store();
}
__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_trunc_load() {
  return __raptor_get_memory_access_trunc_load();
}
__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_original_store() {
  return __raptor_get_memory_access_original_store();
}
__RAPTOR_MPFR_ATTRIBUTES
long long f_raptor_get_memory_access_original_load() {
  return __raptor_get_memory_access_original_load();
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_memory_access(void *ptr, int64_t size, int64_t is_store) {
  if (global_is_truncating) {
    if (is_store)
      trunc_store_counter.fetch_add(size, std::memory_order_relaxed);
    else
      trunc_load_counter.fetch_add(size, std::memory_order_relaxed);
  } else {
    if (is_store)
      original_store_counter.fetch_add(size, std::memory_order_relaxed);
    else
      original_load_counter.fetch_add(size, std::memory_order_relaxed);
  }
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_op_clear() { opdata.clear(); }
