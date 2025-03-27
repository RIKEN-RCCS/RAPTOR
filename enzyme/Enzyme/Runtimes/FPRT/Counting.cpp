
#include <enzyme/fprt/fprt.h>
#include <enzyme/fprt/mpfr.h>

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_trunc_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return trunc_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_double_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return double_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_float_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return float_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_half_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return half_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_trunc_flop_count() {
  return __enzyme_get_trunc_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_double_flop_count() {
  return __enzyme_get_double_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_float_flop_count() {
  return __enzyme_get_float_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_half_flop_count() {
  return __enzyme_get_half_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  double_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_32_23_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  float_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_16_10_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
  half_flop_counter.fetch_add(1, std::memory_order_relaxed);
}
