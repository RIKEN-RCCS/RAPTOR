#include "raptor/Common.h"

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_ieee_64_get(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc, mpfr_t *scratch) {
  __raptor_fp *a = __raptor_fprt_ieee_64_to_ptr(_a);
  return mpfr_get_d(a->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);
}

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_ieee_64_new(double _a, int64_t exponent,
                                 int64_t significand, int64_t mode,
                                 const char *loc, mpfr_t *scratch) {
  __raptor_fp *a = (__raptor_fp *)malloc(sizeof(__raptor_fp));
  if (!a)
    exit(__RAPTOR_MPFR_MALLOC_FAILURE_EXIT_STATUS);
  mpfr_init2(a->result, significand);
  mpfr_set_d(a->result, _a, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);
  a->excl_result = _a;
  a->shadow = _a;
  return __raptor_fprt_ptr_to_double(a);
}

__RAPTOR_MPFR_ATTRIBUTES
double __raptor_fprt_ieee_64_const(double _a, int64_t exponent,
                                   int64_t significand, int64_t mode,
                                   const char *loc, mpfr_t *scratch) {
  // TODO This should really be called only once for an appearance in the code,
  // currently it is called every time a flop uses a constant.
  return __raptor_fprt_ieee_64_new(_a, exponent, significand, mode, loc,
                                   scratch);
}

__RAPTOR_MPFR_ATTRIBUTES
__raptor_fp *__raptor_fprt_ieee_64_new_intermediate(int64_t exponent,
                                                    int64_t significand,
                                                    int64_t mode,
                                                    const char *loc) {
  __raptor_fp *a = (__raptor_fp *)malloc(sizeof(__raptor_fp));
  if (!a)
    exit(__RAPTOR_MPFR_MALLOC_FAILURE_EXIT_STATUS);
  mpfr_init2(a->result, significand);
  return a;
}

__RAPTOR_MPFR_ATTRIBUTES
void __raptor_fprt_ieee_64_delete(double a, int64_t exponent,
                                  int64_t significand, int64_t mode,
                                  const char *loc, mpfr_t *scratch) {
  free(__raptor_fprt_ieee_64_to_ptr(a));
}
