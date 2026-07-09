; RUN: %opt %s %newLoadRaptor -passes="raptor" -S | FileCheck %s

declare double @pow(double %Val, double %Power)
; declare double @llvm.pow.f64(double %Val, double %Power)
; declare double @llvm.powi.f64.i16(double %Val, i16 %power)
declare void @llvm.nvvm.barrier0()

define double @f(double %x, double %y) {
  %res0 = call double @pow(double %x, double %y)
  %res1 = call double @llvm.pow.f64(double %x, double %y)
  %res2 = call double @llvm.powi.f64.i16(double %x, i16 2)
  %res = fadd double %res1, %res2
  call void @llvm.nvvm.barrier0()
  ret double %res
}

define <2 x double> @f_vec(<2 x double> %x, <2 x double> %y) {
  %res1 = call <2 x double> @llvm.pow.v2f64(<2 x double> %x, <2 x double> %y)
  %res2 = call <2 x double> @llvm.powi.v2f64.i16(<2 x double> %x, i16 2)
  %res = fadd <2 x double> %res1, %res2
  call void @llvm.nvvm.barrier0()
  ret <2 x double> %res
}

define <vscale x 4 x double> @f_scalable(<vscale x 4 x double> %x, <vscale x 4 x double> %y) {
  %res1 = call <vscale x 4 x double> @llvm.pow.nxv4f64(<vscale x 4 x double> %x, <vscale x 4 x double> %y)
  %res2 = call <vscale x 4 x double> @llvm.powi.nxv4f64.i16(<vscale x 4 x double> %x, i16 2)
  %res = fadd <vscale x 4 x double> %res1, %res2
  call void @llvm.nvvm.barrier0()
  ret <vscale x 4 x double> %res
}

declare double (double, double)* @__raptor_truncate_mem_func(...)
declare double (double, double)* @__raptor_truncate_op_func(...)

define double @tester(double %x, double %y) {
entry:
  %ptr = call double (double, double)* (...) @__raptor_truncate_mem_func(double (double, double)* @f, i64 64, i64 0, i64 32)
  %res = call double %ptr(double %x, double %y)
  ret double %res
}

define <2 x double> @tester_vec(<2 x double> %x, <2 x double> %y) {
entry:
  %ptr = call <2 x double> (<2 x double>, <2 x double>)* (...) @__raptor_truncate_mem_func(<2 x double> (<2 x double>, <2 x double>)* @f_vec, i64 64, i64 1, i64 8, i64 23)
  %res = call <2 x double> %ptr(<2 x double> %x, <2 x double> %y)
  ret <2 x double> %res
}

define <vscale x 4 x double> @tester_scalable(<vscale x 4 x double> %x, <vscale x 4 x double> %y) {
entry:
  %ptr = call <vscale x 4 x double> (<vscale x 4 x double>, <vscale x 4 x double>)* (...) @__raptor_truncate_mem_func(<vscale x 4 x double> (<vscale x 4 x double>, <vscale x 4 x double>)* @f_scalable, i64 64, i64 1, i64 8, i64 23)
  %res = call <vscale x 4 x double> %ptr(<vscale x 4 x double> %x, <vscale x 4 x double> %y)
  ret <vscale x 4 x double> %res
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

; CHECK: define internal double @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_0_0_f(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_func_pow(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_binop_fadd(
; CHECK-DAG:   call void @llvm.nvvm.barrier

; CHECK: define internal <2 x double> @__raptor_done_truncate_mem_func_ieee_64_to_mpfr_8_23_0_0_0_f_vec(<2 x double> {{.*}}, <2 x double> {{.*}}
; CHECK-DAG: call <2 x double> @__raptor_fprt_vec2x_ieee_64_intr_llvm_pow_v2f64(<2 x double> {{.*}}, <2 x double> {{.*}}
; CHECK-DAG: call <2 x double> @__raptor_fprt_vec2x_ieee_64_intr_llvm_powi_v2f64_i16(<2 x double> {{.*}}, i16 {{.*}}

; CHECK: define weak_odr <2 x double> @__raptor_fprt_vec2x_ieee_64_intr_llvm_pow_v2f64(<2 x double> {{.*}}, <2 x double> {{.*}}
; CHECK-DAG:  call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(double {{.*}}, double {{.*}}
; CHECK-DAG:  call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(double {{.*}}, double {{.*}}

; CHECK: define weak_odr <2 x double> @__raptor_fprt_vec2x_ieee_64_intr_llvm_powi_v2f64_i16(<2 x double> {{.*}}, i16 {{.*}}
; CHECK-DAG:  call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(double {{.*}}, i16 {{.*}}
; CHECK-DAG:  call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(double {{.*}}, i16 {{.*}}

; CHECK: define internal double @__raptor_done_truncate_op_func_ieee_64_to_ieee_32_0_0_0_f(
; CHECK-DAG:   fptrunc
; CHECK-DAG:   call float @llvm.pow.f32(
; CHECK-DAG:   fpext float
; CHECK-DAG:   call float @llvm.powi.f32.i16(

; CHECK: define internal double @__raptor_done_truncate_op_func_ieee_64_to_mpfr_3_7_1_1_0_f(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_func_pow(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_pow_f64(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_intr_llvm_powi_f64_i16(
; CHECK-DAG:   call double @__raptor_fprt_ieee_64_binop_fadd(
; CHECK-DAG:   call void @llvm.nvvm.barrier
