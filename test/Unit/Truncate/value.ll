; RUN: if [ %llvmver -gt 12 ]; then if [ %llvmver -lt 16 ]; then %opt < %s %loadRaptor -raptor -S | FileCheck %s; fi; fi
; RUN: if [ %llvmver -gt 12 ]; then %opt < %s %newLoadRaptor -passes="raptor" -S | FileCheck %s; fi

declare double @__raptor_truncate_mem_value(double, i64, i64)
declare double @__raptor_expand_mem_value(double, i64, i64)

define double @expand_tester(double %a, double * %c) {
entry:
    %b = call double @__raptor_expand_mem_value(double %a, i64 64, i64 1, i64 10, i64 32)
  ret double %b
}

define double @truncate_tester(double %a) {
entry:
  %b = call double @__raptor_truncate_mem_value(double %a, i64 64, i64 1, i64 10, i64 32)
  ret double %b
}

; CHECK: define double @expand_tester(
; CHECK:   call double @__raptor_fprt_ieee_64_get(double {{.*}}%a, i64 10, i64 32, i64 1, {{.*}}

; CHECK: define double @truncate_tester(
; CHECK:   call double @__raptor_fprt_ieee_64_new(double {{.*}}, i64 10, i64 32, i64 1, {{.*}}
