#include "raptor/Common.h"
#include "raptor/raptor.h"

#include <cstdlib>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

typedef void (*LogFuncTy_ieee_64)(double);
typedef void (*LogFuncTy_ieee_32)(float);
// typedef void (*LogFuncTy_ieee_16)(half);

void __raptor_fprt_trunc_change(int64_t is_push, int64_t to_e, int64_t to_m,
                                int64_t mode, const char *loc, void *scratch) {}

namespace {
struct FloatLoggerTy {

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  std::unique_ptr<std::ofstream> OS_##FROM_TY;
#include "raptor/FloatTypes.def"

  template <typename T> const char *getTypeStr() {
#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  if constexpr (std::is_same<T, CPP_TY>::value)                                \
    return #CPP_TY;
#include "raptor/FloatTypes.def"
    abort();
  }

  template <typename T> void clear() {
#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  if constexpr (std::is_same<T, CPP_TY>::value)                                \
    OS_##FROM_TY.reset(nullptr);
#include "raptor/FloatTypes.def"
  }

  template <typename T> void setLogPath(const std::string Path) {
#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  if constexpr (std::is_same<T, CPP_TY>::value) {                              \
    std::cerr << "Writing flop log for " #CPP_TY " to '" << Path << "'...\n";  \
    OS_##FROM_TY = std::make_unique<std::ofstream>(                            \
        Path, std::ios_base::out | std::ios_base::binary);                     \
  }
#include "raptor/FloatTypes.def"
  }

  template <typename T> void log(T F) {
#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  if constexpr (std::is_same<T, CPP_TY>::value)                                \
    if (OS_##FROM_TY)                                                          \
      OS_##FROM_TY->write(reinterpret_cast<const char *>(&F), sizeof(F));
#include "raptor/FloatTypes.def"
  }

  FloatLoggerTy() {
    if (char *C = getenv("RAPTOR_FLOP_LOG_PREFIX")) {
#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  setLogPath<CPP_TY>(std::string(C) + "." #CPP_TY);
#include "raptor/FloatTypes.def"
    }
  }

  ~FloatLoggerTy() {}

} FloatLogger;
} // namespace

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprtlog_##FROM_TY##_abs_err(CPP_TY a, CPP_TY b) {            \
    return std::abs(a - b);                                                    \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprtlog_##FROM_TY##_trunc_change(                              \
      int64_t is_push, int64_t to_e, int64_t to_m, int64_t mode,               \
      const char *loc, void *scratch) {                                        \
    __raptor_fprt_trunc_change(is_push, to_e, to_m, mode, loc, scratch);       \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void *__raptor_fprtlog_##FROM_TY##_get_scratch(                              \
      int64_t to_e, int64_t to_m, int64_t mode, const char *loc,               \
      void *scratch) {                                                         \
    return nullptr;                                                            \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprtlog_##FROM_TY##_free_scratch(                              \
      int64_t to_e, int64_t to_m, int64_t mode, const char *loc,               \
      void *scratch) {}                                                        \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_log_flops_##FROM_TY(CPP_TY a) { FloatLogger.log(a); }          \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_clear_flop_log_##CPP_TY() { FloatLogger.clear<CPP_TY>(); }     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_set_flop_log_##CPP_TY(const char *path) {                      \
    FloatLogger.setLogPath<CPP_TY>(path);                                      \
  }
#include "raptor/FloatTypes.def"

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
        a, b);                                                                 \
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

#define __RAPTOR_MPFR_FMULADD(OP_TYPE, LLVM_OP_NAME, FROM_TYPE, TYPE,                        \
                              MPFR_TYPE, LLVM_TYPE, ROUNDING_MODE)                           \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                                          \
  TYPE                                                                                       \
  __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME##_##LLVM_TYPE(          \
      TYPE a, TYPE b, TYPE c);                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                                   \
  TYPE __raptor_fprtlog_##FROM_TYPE##_intr_##LLVM_OP_NAME##_##LLVM_TYPE(                     \
      TYPE a, TYPE b, TYPE c, LogFuncTy_##FROM_TYPE f, int64_t mode,                         \
      const char *loc, void *scratch) {                                                      \
    f(a);                                                                                    \
    f(b);                                                                                    \
    f(c);                                                                                    \
    return __raptor_fprtlog_original_##FROM_TYPE##_##OP_TYPE##_##LLVM_OP_NAME##_##LLVM_TYPE( \
        a, b, c);                                                                            \
  }

#define __RAPTOR_MPFR_FCMP_IMPL(NAME, ORDERED, CMP, FROM_TYPE, TYPE, MPFR_GET, \
                                ROUNDING_MODE)                                 \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES                                            \
  bool __raptor_fprtlog_original_##FROM_TYPE##_fcmp_##NAME(TYPE a, TYPE b);    \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  bool __raptor_fprtlog_##FROM_TYPE##_fcmp_##NAME(                             \
      TYPE a, TYPE b, LogFuncTy_##FROM_TYPE f, const char *loc,                \
      void *scratch) {                                                         \
    return __raptor_fprtlog_original_##FROM_TYPE##_fcmp_##NAME(a, b);          \
  }

#define __RAPTOR_MPFR_ISCLASS(FROM_TYPE, TYPE, LLVM_TYPE)                            \
  __RAPTOR_MPFR_ORIGINAL_ATTRIBUTES bool                                             \
  __raptor_fprtlog_original_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE(          \
      TYPE a, int32_t tests);                                                        \
  __RAPTOR_MPFR_ATTRIBUTES bool                                                      \
  __raptor_fprtlog_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE(                   \
      TYPE a, int32_t tests, LogFuncTy_##FROM_TYPE f, const char *loc,               \
      void *scratch) {                                                               \
    return __raptor_fprtlog_original_##FROM_TYPE##_intr_llvm_is_fpclass_##LLVM_TYPE( \
        a, tests);                                                                   \
  }

#include "Flops.def"
