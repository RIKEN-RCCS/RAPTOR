#ifndef _RAPTOR_COMMON_H_
#define _RAPTOR_COMMON_H_

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mpfr.h>

#define MAX_MPFR_OPERANDS 3

#define __RAPTOR_MPFR_ATTRIBUTES extern "C"
#define __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES extern "C"
#define __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE GMP_RNDN
#define __RAPTOR_MPFR_MALLOC_FAILURE_EXIT_STATUS 114

extern std::atomic<long long> shadow_err_counter;
extern std::atomic<bool> global_is_truncating;

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

static inline bool __raptor_fprt_is_mem_mode(int64_t mode) {
  return mode & 0b0001;
}
static inline bool __raptor_fprt_is_op_mode(int64_t mode) {
  return mode & 0b0010;
}
static inline bool __raptor_fprt_is_full_module_op_mode(int64_t mode) {
  return mode & 0b0100;
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_dump_status();
__RAPTOR_MPFR_ATTRIBUTES
double raptor_fprt_gc_mark_seen(double a);
__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_doit();

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_start();
__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_end();

template <typename To, typename From> To raptor_bitcast(From from) {
  static_assert(sizeof(From) == sizeof(To));
  size_t size = sizeof(From);
  To to;
  std::memcpy(&to, &from, size);
  return to;
}

template <typename To, typename From> To checked_raptor_bitcast(From from) {
  if constexpr (sizeof(From) == sizeof(To))
    return raptor_bitcast<To, From>(from);
  else
    abort();
}

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  static inline CPP_TY __raptor_fprt_idx_to_##FROM_TY(uint64_t p) {            \
    return checked_raptor_bitcast<CPP_TY>(p);                                  \
  }                                                                            \
  static inline uint64_t __raptor_fprt_##FROM_TY##_to_idx(CPP_TY d) {          \
    return checked_raptor_bitcast<uint64_t>(d);                                \
  }                                                                            \
  static inline CPP_TY __raptor_fprt_ptr_to_##FROM_TY(__raptor_fp *p) {        \
    return checked_raptor_bitcast<CPP_TY>(p);                                  \
  }                                                                            \
  static inline __raptor_fp *__raptor_fprt_##FROM_TY##_to_ptr(CPP_TY d) {      \
    return checked_raptor_bitcast<__raptor_fp *>(d);                           \
  }
#include "raptor/FloatTypes.def"
#undef RAPTOR_FLOAT_TYPE

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_get(CPP_TY _a, int64_t exponent,            \
                                       int64_t significand, int64_t mode,      \
                                       const char *loc, void *scratch);        \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_new(CPP_TY _a, int64_t exponent,            \
                                       int64_t significand, int64_t mode,      \
                                       const char *loc, void *scratch);        \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_const(CPP_TY _a, int64_t exponent,          \
                                         int64_t significand, int64_t mode,    \
                                         const char *loc, void *scratch);      \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  __raptor_fp *__raptor_fprt_##FROM_TY##_new_intermediate(                     \
      int64_t exponent, int64_t significand, int64_t mode, const char *loc,    \
      void *scratch);                                                          \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_delete(CPP_TY a, int64_t exponent,            \
                                        int64_t significand, int64_t mode,     \
                                        const char *loc, void *scratch);       \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void *__raptor_fprt_##FROM_TY##_get_scratch(int64_t to_e, int64_t to_m,      \
                                              int64_t mode, const char *loc,   \
                                              void *scratch);                  \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_free_scratch(int64_t to_e, int64_t to_m,      \
                                              int64_t mode, const char *loc,   \
                                              void *scratch);                  \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_trunc_change(int64_t is_push, int64_t to_e,   \
                                              int64_t to_m, int64_t mode,      \
                                              const char *loc, void *scratch);

#include "raptor/FloatTypes.def"
#undef RAPTOR_FLOAT_TYPE

#endif // _RAPTOR_COMMON_H_
