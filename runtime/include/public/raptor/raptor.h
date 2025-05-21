#ifndef _RAPTOR_FPRT_FPRT_H_
#define _RAPTOR_FPRT_FPRT_H_

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

double __raptor_fprt_64_52_get(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, void *scratch);
double __raptor_fprt_64_52_new(double _a, int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, void *scratch);
void __raptor_fprt_64_52_delete(double a, int64_t exponent, int64_t significand,
                                int64_t mode, const char *loc, void *scratch);
double __raptor_truncate_mem_value_d(double, int, int);
float __raptor_truncate_mem_value_f(float, int, int);
double __raptor_expand_mem_value_d(double, int, int);
float __raptor_expand_mem_value_f(float, int, int);
void __raptor_fprt_delete_all();

long long __raptor_get_trunc_flop_count();
long long f_raptor_get_trunc_flop_count();


#ifdef __cplusplus
}
#endif

#endif // _RAPTOR_FPRT_FPRT_H_
