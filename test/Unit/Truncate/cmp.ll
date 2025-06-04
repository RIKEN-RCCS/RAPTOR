; RUN: if [ %llvmver -lt 16 ]; then %opt < %s %loadRaptor -raptor -S | FileCheck %s; fi
; RUN: %opt < %s %newLoadRaptor -passes="raptor" -S | FileCheck %s

define i1 @f(double %x, double %y) {
  %res = fcmp olt double %x, %y
  ret i1 %res
}

declare i1 (double, double)* @__raptor_truncate_mem_func(...)
declare i1 (double, double)* @__raptor_truncate_op_func(...)

define i1 @tester(double %x, double %y) {
entry:
  %ptr = call i1 (double, double)* (...) @__raptor_truncate_mem_func(i1 (double, double)* @f, i64 64, i64 0, i64 32)
  %res = call i1 %ptr(double %x, double %y)
  ret i1 %res
}
define i1 @tester_op(double %x, double %y) {
entry:
  %ptr = call i1 (double, double)* (...) @__raptor_truncate_op_func(i1 (double, double)* @f, i64 64, i64 0, i64 32)
  %res = call i1 %ptr(double %x, double %y)
  ret i1 %res
}
define i1 @tester_op_mpfr(double %x, double %y) {
entry:
  %ptr = call i1 (double, double)* (...) @__raptor_truncate_op_func(i1 (double, double)* @f, i64 64, i64 1, i64 3, i64 7)
  %res = call i1 %ptr(double %x, double %y)
  ret i1 %res
}

; CHECK: define internal i1 @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_0_0_f(
; CHECK:   call i1 @__raptor_fprt_ieee_64_fcmp_olt

; CHECK: define internal i1 @__raptor_done_truncate_op_func_ieee_64_to_ieee_32_1_1_0_f(
; CHECK:   fcmp olt double

; CHECK: define internal i1 @__raptor_done_truncate_op_func_ieee_64_to_mpfr_3_7_1_1_0_f(
; CHECK:   fcmp olt double
