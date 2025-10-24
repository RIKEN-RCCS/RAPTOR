#ifndef _RAPTOR_FPRT_FPRT_H_
#define _RAPTOR_FPRT_FPRT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
template <typename fty>
fty *__raptor_truncate_op_func(fty *, int, int, int, int);
template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int);
#endif

#ifdef __cplusplus
extern "C" {
#endif

double __raptor_truncate_mem_value_d(double, int, int);
float __raptor_truncate_mem_value_f(float, int, int);
double __raptor_expand_mem_value_d(double, int, int);
float __raptor_expand_mem_value_f(float, int, int);
void __raptor_fprt_delete_all();

long long __raptor_get_trunc_flop_count();
long long f_raptor_get_trunc_flop_count();

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  struct __raptor_logged_flops_##CPP_TY {                                      \
    CPP_TY *vals;                                                              \
    size_t num;                                                                \
  };                                                                           \
  void __raptor_clear_flop_log_##CPP_TY();                                     \
  void __raptor_set_flop_log_##CPP_TY(const char *path);
#include "FloatTypes.def"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
template <typename fty> fty *__raptor_log_flops(fty *);
template <typename fty>
fty *__raptor_truncate_mem_func(fty *, int, int, int, int);
template <typename fty>
fty *__raptor_truncate_op_func(fty *, int, int, int, int);
template <typename... Tys> double __raptor_truncate_mem_value(Tys...);
template <typename... Tys> double __raptor_expand_mem_value(Tys...);
#endif

#endif // _RAPTOR_FPRT_FPRT_H_
