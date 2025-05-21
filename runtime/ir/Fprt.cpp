#include <atomic>
#include <mpfr.h>

#define __RAPTOR_MPFR_ATTRIBUTES                                               \
  [[maybe_unused]] __attribute__((weak)) __attribute__((used))
#define __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                      \
  __attribute__((weak)) __attribute__((used))
#define __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE GMP_RNDN

#define __RAPTOR_MPFR_MALLOC_FAILURE_EXIT_STATUS 114

// Global variable to count truncated flops
// TODO only implemented for op mode at the moment
// extern std::atomic<long long> trunc_flop_counter;
// extern std::atomic<long long> trunc_excl_flop_counter;
// extern std::atomic<long long> double_flop_counter;
// extern std::atomic<long long> float_flop_counter;
// extern std::atomic<long long> half_flop_counter;

std::atomic<long long> shadow_err_counter = 0;
bool excl_trunc = false;

// For internal use
// struct __raptor_fp;
typedef struct __raptor_fp {
  mpfr_t result;
//#ifdef RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS
  double excl_result;
  double shadow;
//#endif
} __raptor_fp;

__raptor_fp *__raptor_fprt_64_52_new_intermediate(int64_t exponent,
                                                  int64_t significand,
                                                  int64_t mode,
                                                  const char *loc);
double __raptor_fprt_64_52_const(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc, mpfr_t *scratch);

__RAPTOR_MPFR_ATTRIBUTES bool __raptor_fprt_is_mem_mode(int64_t mode) {
  return mode & 0b0001;
}
__RAPTOR_MPFR_ATTRIBUTES bool __raptor_fprt_is_op_mode(int64_t mode) {
  return mode & 0b0010;
}
__RAPTOR_MPFR_ATTRIBUTES double __raptor_fprt_idx_to_double(uint64_t p) {
  return *((double *)(&p));
}
__RAPTOR_MPFR_ATTRIBUTES uint64_t __raptor_fprt_double_to_idx(double d) {
  return *((uint64_t *)(&d));
}
__RAPTOR_MPFR_ATTRIBUTES double __raptor_fprt_ptr_to_double(__raptor_fp *p) {
  return *((double *)(&p));
}
__RAPTOR_MPFR_ATTRIBUTES __raptor_fp *__raptor_fprt_double_to_ptr(double d) {
  return *((__raptor_fp **)(&d));
}
