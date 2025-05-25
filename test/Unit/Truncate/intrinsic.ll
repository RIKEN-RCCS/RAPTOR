; RUN: %opt %s %newLoadRaptor -passes="raptor" -S | FileCheck %s

declare double @pow(double %Val, double %Power)
declare double @llvm.pow.f64(double %Val, double %Power)
declare double @llvm.powi.f64.i16(double %Val, i16 %power)
declare void @llvm.nvvm.barrier0()

define double @f(double %x, double %y) {
  %res0 = call double @pow(double %x, double %y)
  %res1 = call double @llvm.pow.f64(double %x, double %y)
  %res2 = call double @llvm.powi.f64.i16(double %x, i16 2)
  %res = fadd double %res1, %res2
  call void @llvm.nvvm.barrier0()
  ret double %res
}

declare double (double, double)* @__raptor_truncate_mem_func(...)
declare double (double, double)* @__raptor_truncate_op_func(...)

define double @tester(double %x, double %y) {
entry:
  %ptr = call double (double, double)* (...) @__raptor_truncate_mem_func(double (double, double)* @f, i64 64, i64 0, i64 32)
  %res = call double %ptr(double %x, double %y)
  ret double %res
}
; TODO This used to test if we detect that we truncate to a native float type
; and use that instead of MPFR but now we always generate the FPRT calls.
; Instead we shuold probably add an additional flag/mode to truncate to native
; types
define double @tester_op(double %x, double %y) {
entry:
  %ptr = call double (double, double)* (...) @__raptor_truncate_op_func(double (double, double)* @f, i64 64, i64 0, i64 32)
  %res = call double %ptr(double %x, double %y)
  ret double %res
}
define double @tester_op_mpfr(double %x, double %y) {
entry:
  %ptr = call double (double, double)* (...) @__raptor_truncate_op_func(double (double, double)* @f, i64 64, i64 1, i64 3, i64 7)
  %res = call double %ptr(double %x, double %y)
  ret double %res
}

; CHECK: define internal double @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_f(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_func_pow(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_binop_fadd(
; CHECK-DAG:   call void @llvm.nvvm.barrier0()

; CHECK: define internal double @__raptor_done_truncate_op_func_ieee_64_to_ieee_32_f(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_func_pow(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_binop_fadd(
; CHECK-DAG:   call void @llvm.nvvm.barrier0()

; CHECK: define internal double @__raptor_done_truncate_op_func_ieee_64_to_mpfr_3_7_f(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_func_pow(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_binop_fadd(
; CHECK-DAG:   call void @llvm.nvvm.barrier0()
