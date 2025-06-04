; RUN: %opt %s %newLoadRaptor -passes="raptor" -S | FileCheck %s

; we managed to truncate the parallel region
; CHECK: define {{.*}} double @__raptor_done_truncate_op_func_ieee_64_to_mpfr_10_8_1_1_0__Z10teams__pardd
; CHECK:   call {{.*}} @__kmpc_fork_teams({{.*}}@__raptor_done_truncate_op_func_ieee_64_to_mpfr_10_8_0_1_0__Z10teams__pardd.omp_outlined
; CHECK: define {{.*}} @__raptor_done_truncate_op_func_ieee_64_to_mpfr_10_8_0_1_0__Z10teams__pardd.omp_outlined
; CHECK:   call {{.*}} @__kmpc_fork_call({{.*}}@__raptor_done_truncate_op_func_ieee_64_to_mpfr_10_8_0_1_0__Z10teams__pardd.omp_outlined.omp_outlined
; CHECK: define {{.*}} @__raptor_done_truncate_op_func_ieee_64_to_mpfr_10_8_0_1_0__Z10teams__pardd.omp_outlined.omp_outlined
; CHECK:   call double @__raptor_fprt_ieee_64_binop_fadd


%struct.ident_t = type { i32, i32, i32, i32, ptr }

@0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
@1 = private unnamed_addr constant %struct.ident_t { i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8

; Function Attrs: mustprogress nounwind uwtable
define dso_local noundef double @_Z10teams__pardd(double noundef %a, double noundef %b) {
entry:
  %a.addr = alloca double, align 8
  %b.addr = alloca double, align 8
  %c = alloca double, align 8
  store double %a, ptr %a.addr, align 8
  store double %b, ptr %b.addr, align 8
  store double 0.000000e+00, ptr %c, align 8
  call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr nonnull @1, i32 3, ptr nonnull @_Z10teams__pardd.omp_outlined, ptr nonnull %c, ptr nonnull %a.addr, ptr nonnull %b.addr)
  %0 = load double, ptr %c, align 8
  ret double %0
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)

; Function Attrs: alwaysinline norecurse nounwind uwtable
define internal void @_Z10teams__pardd.omp_outlined(ptr noalias nocapture readnone %.global_tid., ptr noalias nocapture readnone %.bound_tid., ptr noundef nonnull align 8 dereferenceable(8) %c, ptr noundef nonnull align 8 dereferenceable(8) %a, ptr noundef nonnull align 8 dereferenceable(8) %b) {
entry:
  tail call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr nonnull @1, i32 3, ptr nonnull @_Z10teams__pardd.omp_outlined.omp_outlined, ptr nonnull %c, ptr nonnull %a, ptr nonnull %b)
  ret void
}

; Function Attrs: alwaysinline mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define internal void @_Z10teams__pardd.omp_outlined.omp_outlined(ptr noalias nocapture readnone %.global_tid., ptr noalias nocapture readnone %.bound_tid., ptr nocapture noundef nonnull writeonly align 8 dereferenceable(8) initializes((0, 8)) %c, ptr nocapture noundef nonnull readonly align 8 dereferenceable(8) %a, ptr nocapture noundef nonnull readonly align 8 dereferenceable(8) %b) {
entry:
  %0 = load double, ptr %a, align 8
  %1 = load double, ptr %b, align 8
  %add = fadd double %0, %1
  store double %add, ptr %c, align 8
  ret void
}

; Function Attrs: nounwind
declare !callback !10 void @__kmpc_fork_call(ptr, i32, ptr, ...) local_unnamed_addr

; Function Attrs: nounwind
declare !callback !10 void @__kmpc_fork_teams(ptr, i32, ptr, ...) local_unnamed_addr

; Function Attrs: mustprogress norecurse uwtable
define dso_local noundef i32 @main() local_unnamed_addr {
entry:
  %call = tail call noundef ptr @_Z25__raptor_truncate_op_funcIFdddEEPT_S2_iiii(ptr noundef nonnull @_Z10teams__pardd, i32 noundef 64, i32 noundef 1, i32 noundef 10, i32 noundef 8)
  %call1 = tail call noundef double %call(double noundef 1.000000e+00, double noundef 1.000000e+03)
  ret i32 0
}

declare noundef ptr @_Z25__raptor_truncate_op_funcIFdddEEPT_S2_iiii(ptr noundef, i32 noundef, i32 noundef, i32 noundef, i32 noundef) local_unnamed_addr


!10 = !{!11}
!11 = !{i64 2, i64 -1, i64 -1, i1 true}
