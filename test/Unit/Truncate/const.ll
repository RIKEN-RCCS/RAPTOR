; RUN: if [ %llvmver -gt 12 ]; then if [ %llvmver -lt 16 ]; then %opt < %s %loadRaptor -raptor -S | FileCheck %s; fi; fi
; RUN: if [ %llvmver -gt 12 ]; then %opt < %s %newLoadRaptor -passes="raptor" -S | FileCheck %s; fi

define double @f(double %x) {
  %res = fadd double %x, 1.0
  ret double %res
}

declare double (double)* @__raptor_truncate_mem_func(...)
declare double (double)* @__raptor_truncate_op_func(...)

define double @tester(double %x) {
entry:
  %ptr = call double (double)* (...) @__raptor_truncate_mem_func(double (double)* @f, i64 64, i64 0, i64 32)
  %res = call double %ptr(double %x)
  ret double %res
}
define double @tester_op_mpfr(double %x) {
entry:
  %ptr = call double (double)* (...) @__raptor_truncate_op_func(double (double)* @f, i64 64, i64 1, i64 3, i64 7)
  %res = call double %ptr(double %x)
  ret double %res
}

; CHECK: define internal double @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_f(double %x) {
; CHECK:   call double @__raptor_fprt_ieee_64_const(double 1.000000e+00, i64 8, i64 23, i64 1, {{.*}}
; CHECK:   call double @__raptor_fprt_ieee_64_binop_fadd(double {{.*}}, double %1, i64 8, i64 23, i64 1, {{.*}}

; CHECK: define internal double @__raptor_done_truncate_op_func_ieee_64_to_mpfr_3_7_f(double %x) {
; CHECK:   call double @__raptor_fprt_ieee_64_binop_fadd(double {{.*}}, double 1.000000e+00, i64 3, i64 7, i64 2
