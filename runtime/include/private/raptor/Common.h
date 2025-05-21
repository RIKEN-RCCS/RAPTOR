#ifndef _RAPTOR_COMMON_H_
#define _RAPTOR_COMMON_H_

#include <atomic>
#include <mpfr.h>

#define MAX_MPFR_OPERANDS 3

#define __RAPTOR_MPFR_ATTRIBUTES __attribute__((weak))
#define __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES __attribute__((weak))
#define __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE GMP_RNDN
#define __RAPTOR_MPFR_MALLOC_FAILURE_EXIT_STATUS 114

extern std::atomic<long long> shadow_err_counter;

typedef struct __raptor_op {
  const char *op;             // Operation name
  double l1_err = 0;          // Running error.
  long long count_thresh = 0; // Number of error violations
  long long count = 0;        // Number of samples
  long long count_ignore = 0;
} __raptor_op;

// For internal use
// struct __raptor_fp;
typedef struct __raptor_fp {
  mpfr_t result;
  // #ifdef RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS
  double excl_result;
  double shadow;
  // #endif
} __raptor_fp;

extern "C" __raptor_fp *
__raptor_fprt_64_52_new_intermediate(int64_t exponent, int64_t significand,
                                     int64_t mode, const char *loc);

extern "C" double __raptor_fprt_64_52_const(double _a, int64_t exponent,
                                            int64_t significand, int64_t mode,
                                            const char *loc, mpfr_t *scratch);

static inline bool __raptor_fprt_is_mem_mode(int64_t mode) {
  return mode & 0b0001;
}
static inline bool __raptor_fprt_is_op_mode(int64_t mode) {
  return mode & 0b0010;
}
static inline double __raptor_fprt_idx_to_double(uint64_t p) {
  return *((double *)(&p));
}
static inline uint64_t __raptor_fprt_double_to_idx(double d) {
  return *((uint64_t *)(&d));
}
static inline double __raptor_fprt_ptr_to_double(__raptor_fp *p) {
  return *((double *)(&p));
}
static inline __raptor_fp *__raptor_fprt_double_to_ptr(double d) {
  return *((__raptor_fp **)(&d));
}

#endif // _RAPTOR_COMMON_H_
