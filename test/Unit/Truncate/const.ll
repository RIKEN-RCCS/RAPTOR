; RUN: if [ %llvmver -gt 12 ]; then if [ %llvmver -lt 16 ]; then %opt < %s %loadRaptor -raptor -S | FileCheck %s; fi; fi
; RUN: if [ %llvmver -gt 12 ]; then %opt < %s %newLoadRaptor -passes="raptor" -S | FileCheck %s; fi

define double @f(double %x) {
  %res = fadd double %x, 1.0
  ret double %res
}

declare !callback !0 double @callback_func_variadic_passed(ptr, ...)
declare !callback !2 double @callback_func_variadic_not_passed(ptr, double, ...)

define double @h(double %x, double %y) {
  %res = fadd double %x, %y
  ret double %res
}

define double @g() {
  %1 = call double @f(double 2.000000e+00)
  %2 = call double (ptr, double, ...) @callback_func_variadic_not_passed(ptr nonnull @f, double 3.000000e+00, double %1)
  %3 = call double (ptr, double, ...) @callback_func_variadic_not_passed(ptr nonnull @f, double %2, double 4.000000e+00)
  %res = call double (ptr, ...) @callback_func_variadic_passed(ptr nonnull @h, double %3, double 5.000000e+00)
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
define double @tester_mem_literal() {
entry:
  %ptr = call double (double)* (...) @__raptor_truncate_mem_func(double ()* @g, i64 64, i64 0, i64 32)
  %res = call double %ptr()
  ret double %res
}
define double @tester_op_mpfr(double %x) {
entry:
  %ptr = call double (double)* (...) @__raptor_truncate_op_func(double (double)* @f, i64 64, i64 1, i64 3, i64 7)
  %res = call double %ptr(double %x)
  ret double %res
}

!3 = !{i64 0, i64 1, i1 false}
!2 = !{!3}

!1 = !{i64 0, i1 true}
!0 = !{!1}

; CHECK: define internal double @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_0_0_f(double %x) {
; CHECK:   call double @__raptor_fprt_ieee_64_const(double 1.000000e+00, i64 8, i64 23, i64 1, {{.*}}
; CHECK:   call double @__raptor_fprt_ieee_64_binop_fadd(double {{.*}}, double %1, i64 8, i64 23, i64 1, {{.*}}

; CHECK: define internal double @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_0_0_g() {
; CHECK:   call double @__raptor_fprt_ieee_64_const(double 2.000000e+00, i64 8, i64 23, i64 1, {{.*}}
; CHECK:   call double @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_1_0_f(double %{{.*}})
; CHECK:   call double @__raptor_fprt_ieee_64_const(double 3.000000e+00, i64 8, i64 23, i64 1, {{.*}}
; CHECK:   call {{.*}} @callback_func_variadic_not_passed({{.*}}@__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_1_0_f, double %{{.*}}, double %{{.*}})
; CHECK:   call {{.*}} @callback_func_variadic_not_passed({{.*}}@__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_1_0_f, double %{{.*}}, double 4.000000e+00)
; CHECK:   call double @__raptor_fprt_ieee_64_const(double 5.000000e+00, i64 8, i64 23, i64 1, {{.*}}
; CHECK:   call {{.*}} @callback_func_variadic_passed({{.*}}@__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_1_0_h, double %{{.*}}, double %{{.*}})

; CHECK: define internal double @__raptor_done_truncate_op_func_ieee_64_to_mpfr_3_7_1_1_0_f(double %x) {
; CHECK:   call double @__raptor_fprt_ieee_64_binop_fadd(double {{.*}}, double 1.000000e+00, i64 3, i64 7, i64 2
