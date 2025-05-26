; RUN: %opt %s %newLoadRaptor -passes="raptor" -S | FileCheck %s

define void @f(double* %x) {
  %y = load double, double* %x
  %m = fmul double %y, %y
  store double %m, double* %x
  ret void
}

declare void (double*)* @__raptor_truncate_mem_func(...)
declare void (double*)* @__raptor_truncate_op_func(...)

define void @tester(double* %data) {
entry:
  %ptr = call void (double*)* (...) @__raptor_truncate_mem_func(void (double*)* @f, i64 64, i64 0, i64 32)
  call void %ptr(double* %data)
  ret void
}
; TODO This used to test if we detect that we truncate to a native float type
; and use that instead of MPFR but now we always generate the FPRT calls.
; Instead we shuold probably add an additional flag/mode to truncate to native
; types
define void @tester_op(double* %data) {
entry:
  %ptr = call void (double*)* (...) @__raptor_truncate_op_func(void (double*)* @f, i64 64, i64 0, i64 32)
  call void %ptr(double* %data)
  ret void
}
define void @tester_op_mpfr(double* %data) {
entry:
  %ptr = call void (double*)* (...) @__raptor_truncate_op_func(void (double*)* @f, i64 64, i64 1, i64 8, i64 23)
  call void %ptr(double* %data)
  ret void
}

; CHECK: define void @f(ptr %x) {
; CHECK-NEXT:   %y = load double, ptr %x, align 8
; CHECK-NEXT:   %m = fmul double %y, %y
; CHECK-NEXT:   store double %m, ptr %x, align 8
; CHECK-NEXT:   ret void
; CHECK-NEXT: }

; CHECK: define void @tester(ptr %data) {
; CHECK-NEXT: entry:
; CHECK-NEXT:   call void @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_f(ptr %data)
; CHECK-NEXT:   ret void
; CHECK-NEXT: }

; CHECK: define void @tester_op(ptr %data) {
; CHECK-NEXT: entry:
; CHECK-NEXT:   call void @__raptor_done_truncate_op_func_ieee_64_to_ieee_32_f(ptr %data)
; CHECK-NEXT:   ret void
; CHECK-NEXT: }

; CHECK: define void @tester_op_mpfr(ptr %data) {
; CHECK-NEXT: entry:
; CHECK-NEXT:   call void @__raptor_done_truncate_op_func_ieee_64_to_mpfr_8_23_f(ptr %data)
; CHECK-NEXT:   ret void
; CHECK-NEXT: }

; CHECK: define internal void @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_f(ptr %x) {
; CHECK-NEXT:   %y = load double, ptr %x, align 8
; CHECK-NEXT:   %m = call double @__raptor_fprt_ieee_64_binop_fmul(double %y, double %y, i64 8, i64 23, i64 1, ptr @0, ptr null)
; CHECK-NEXT:   store double %m, ptr %x, align 8
; CHECK-NEXT:   ret void
; CHECK-NEXT: }

; CHECK: define weak_odr double @__raptor_fprt_original_ieee_64_binop_fmul(double %0, double %1) {
; CHECK-NEXT: entry:
; CHECK-NEXT:   %2 = fmul double %0, %1
; CHECK-NEXT:   ret double %2
; CHECK-NEXT: }

; CHECK: define internal void @__raptor_done_truncate_op_func_ieee_64_to_ieee_32_f(ptr %x) {
; CHECK-NEXT:   %y = load double, ptr %x, align 8
; CHECK-NEXT:   %raptor_trunc = fptrunc double %y to float
; CHECK-NEXT:   %raptor_trunc1 = fptrunc double %y to float
; CHECK-NEXT:   %m = fmul float %raptor_trunc, %raptor_trunc1
; CHECK-NEXT:   %raptor_exp = fpext float %m to double
; CHECK-NEXT:   store double %raptor_exp, ptr %x, align 8
; CHECK-NEXT:   ret void
; CHECK-NEXT: }

; CHECK: define internal void @__raptor_done_truncate_op_func_ieee_64_to_mpfr_8_23_f(ptr %x) {
; CHECK-NEXT:   call void @__raptor_fprt_ieee_64_trunc_change(i64 1, i64 8, i64 23, i64 2, ptr @0, ptr null)
; CHECK-NEXT:   %1 = call ptr @__raptor_fprt_ieee_64_get_scratch(i64 8, i64 23, i64 2, ptr @0, ptr null)
; CHECK-NEXT:   %y = load double, ptr %x, align 8
; CHECK-NEXT:   %m = call double @__raptor_fprt_ieee_64_binop_fmul(double %y, double %y, i64 8, i64 23, i64 2, ptr @0, ptr %1)
; CHECK-NEXT:   store double %m, ptr %x, align 8
; CHECK-NEXT:   %2 = call ptr @__raptor_fprt_ieee_64_free_scratch(i64 8, i64 23, i64 2, ptr @0, ptr %1)
; CHECK-NEXT:   call void @__raptor_fprt_ieee_64_trunc_change(i64 0, i64 8, i64 23, i64 2, ptr @0, ptr %1)
; CHECK-NEXT:   ret void
; CHECK-NEXT: }
