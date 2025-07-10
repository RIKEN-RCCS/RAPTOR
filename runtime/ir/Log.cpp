#include "raptor/Common.h"

typedef void (*LogFuncTy_ieee_64)(double);
typedef void (*LogFuncTy_ieee_32)(float);
// typedef void (*LogFuncTy_ieee_16)(half);

#define __RAPTOR_MPFR_LROUND(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, RET, ARG1,      \
                             MPFR_SET_ARG1, ROUNDING_MODE)                     \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(      \
      ARG1 a);                                                                 \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprtlog_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(               \
      ARG1 a, LogFuncTy_##FROM_TYPE f, const char *loc, void *scratch) {       \
    f(a);                                                                      \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME( \
        a);                                                                    \
  }

#define __RAPTOR_MPFR_SINGOP(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE, \
                             RET, MPFR_GET, ARG1, MPFR_SET_ARG1,               \
                             ROUNDING_MODE)                                    \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(      \
      ARG1 a);                                                                 \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprtlog_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(               \
      ARG1 a, LogFuncTy_##FROM_TYPE f, const char *loc, void *scratch) {       \
    f(a);                                                                      \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME( \
        a);                                                                    \
  }

#define __RAPTOR_MPFR_BIN_INT(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME,           \
                              FROM_TYPE, RET, MPFR_GET, ARG1, MPFR_SET_ARG1,   \
                              ARG2, ROUNDING_MODE)                             \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(      \
      ARG1 a, ARG2 b);                                                         \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprtlog_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(               \
      ARG1 a, ARG2 b, LogFuncTy_##FROM_TYPE f, const char *loc,                \
      void *scratch) {                                                         \
    f(a);                                                                      \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME( \
        a, b);                                                                    \
  }

#define __RAPTOR_MPFR_BIN(OP_TYPE, LLVM_OP_NAME, MPFR_FUNC_NAME, FROM_TYPE,    \
                          RET, MPFR_GET, ARG1, MPFR_SET_ARG1, ARG2,            \
                          MPFR_SET_ARG2, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  RET __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(      \
      ARG1 a, ARG2 b);                                                         \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  RET __raptor_fprtlog_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(               \
      ARG1 a, ARG2 b, LogFuncTy_##FROM_TYPE f, const char *loc,                \
      void *scratch) {                                                         \
    f(a);                                                                      \
    f(b);                                                                      \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME( \
        a, b);                                                                 \
  }

#define __RAPTOR_MPFR_FMULADD(LLVM_OP_NAME, FROM_TYPE, TYPE, MPFR_TYPE,        \
                              LLVM_TYPE, ROUNDING_MODE)                        \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  TYPE __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
      TYPE a, TYPE b, TYPE c);                                                 \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  TYPE __raptor_fprtlog_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(       \
      TYPE a, TYPE b, TYPE c, LogFuncTy_##FROM_TYPE f, int64_t mode,           \
      const char *loc, void *scratch) {                                        \
    f(a);                                                                      \
    f(b);                                                                      \
    f(c);                                                                      \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME( \
        a, b, c);                                                              \
  }

#define __RAPTOR_MPFR_FCMP_IMPL(NAME, ORDERED, CMP, FROM_TYPE, TYPE, MPFR_GET, \
                                ROUNDING_MODE)                                 \
  bool __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME(     \
      TYPE a, TYPE b);                                                         \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  bool __raptor_fprtlog_##FROM_TYPE##_fcmp_##NAME(                             \
      TYPE a, TYPE b, LogFuncTy_##FROM_TYPE f, const char *loc,                \
      void *scratch) {                                                         \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME( \
        a, b);                                                                 \
  }

#define __RAPTOR_MPFR_ISCLASS(FROM_TYPE, TYPE, LLVM_TYPE)                            \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES bool                                             \
      __raptor_fprtlog_original_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE(      \
          TYPE a, int32_t tests);                                                    \
  __RAPTOR_MPFR_ATTRIBUTES bool                                                      \
      __raptor_fprtlog_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE(               \
          TYPE a, int32_t tests, LogFuncTy_##FROM_TYPE f, const char *loc,           \
          void *scratch) {                                                           \
    return __raptor_fprtlog_original_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE( \
      a,\
        tests);                                                                      \
  }

#include "Flops.def"
