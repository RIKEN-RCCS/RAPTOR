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

define <2 x double> @const_vec(ptr %0) {
entry:
  br label %testPHI
testPHI:
  %i = phi i32 [0, %entry], [%iInc, %testPHI]
  %test_phi = phi <2 x double> [splat (double 0.0000), %entry], [%res, %testPHI]
  %res = fadd <2 x double> %test_phi, splat (double 1.0000)
  %iInc = add i32 %i, 1
  %cond = icmp eq i32 %i, 2
  br i1 %cond, label %exit, label %testPHI
exit:
  %x = extractelement <2 x double> <double 1.000, double 2.000>, i32 1
  %y = shufflevector <2 x double> %res, <2 x double> <double 2.000, double 3.000>, <2 x i32> <i32 1, i32 2>
  %z = insertelement <2 x double> splat (double 1.000), double 0.000, i32 1
  %cmp = fcmp oeq <2 x double> %y, splat (double 2.000)
  %sel = select <2 x i1> %cmp, <2 x double> %z, <2 x double> splat (double 2.000100e+00)
  %castF = fptosi <2 x double> splat(double 0.0) to <2 x i32>
  %castT = sitofp <2 x i32> %castF to <2 x double>
  store <2 x double> splat (double 1.000010e+00), ptr %0
  ret <2 x double> <double 0.0000, double 1.0000> 
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

define <2 x double> @tester_vec(ptr %0) {
entry:
  %ptr = call <2 x double> (ptr)* (...) @__raptor_truncate_mem_func(<2 x double> (ptr)* @const_vec, i64 64, i64 0, i64 32)
  %res = call <2 x double> %ptr(ptr %0)
  ret <2 x double> %res
}
define <2 x double> @tester_op_mpfr_vec(ptr %0) {
entry:
  %ptr = call <2 x double> (ptr)* (...) @__raptor_truncate_op_func(<2 x double> (ptr)* @const_vec, i64 64, i64 1, i64 3, i64 7)
  %res = call <2 x double> %ptr(ptr %0)
  ret <2 x double> %res
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

; CHECK: define internal <2 x double> @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_0_0_const_vec(ptr %0) {
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> zeroinitializer, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   phi <2 x double> [ %{{.*}}
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> splat (double {{.*}}), i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_binop_fadd(<2 x double> %{{.*}}, <2 x double> %{{.*}}, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> <double 1.000000e+00, double 2.000000e+00>, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   extractelement <2 x double> %{{.*}}, i32 1
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> <double 2.000000e+00, double 3.000000e+00>, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   shufflevector <2 x double> %{{.*}}, <2 x double> %{{.*}}, <2 x i32> <i32 1, i32 2>
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> splat (double 1.000000e+00), i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   call double @__raptor_fprt_ieee_64_const(double 0.000000e+00, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   insertelement <2 x double> %{{.*}}, double %{{.*}}, i32 1
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> splat (double 2.000000e+00), i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   call <2 x i1> @__raptor_fprt_vec2x_ieee_64_fcmp_oeq(<2 x double> %{{.*}}, <2 x double> %{{.*}}, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> splat (double 2.000100e+00), i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   %sel = select <2 x i1> %{{.*}}, <2 x double> %{{.*}}, <2 x double> %{{.*}}
; CHECK:   fptosi <2 x double> zeroinitializer to <2 x i32>
; CHECK:   sitofp <2 x i32> {{.*}} to <2 x double>
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_new(<2 x double> %{{.*}}, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> splat (double 1.000010e+00), i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   store <2 x double> %{{.*}}, ptr %0, align 16
; CHECK:   call <2 x double> @__raptor_fprt_vec2x_ieee_64_const(<2 x double> <double 0.000000e+00, double 1.000000e+00>, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK:   ret <2 x double> %{{.*}}