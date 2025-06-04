; RUN: %opt %s %newLoadRaptor -passes="raptor" -S | FileCheck %s

define double @f(double %x, double %y, i1 %cond) {
  %res = select i1 %cond, double %x, double %y
  ret double %res
}

declare double (double, double, i1)* @__raptor_truncate_mem_func(...)
declare double (double, double, i1)* @__raptor_truncate_op_func(...)

define double @tester(double %x, double %y, i1 %cond) {
entry:
  %ptr = call double (double, double, i1)* (...) @__raptor_truncate_mem_func(double (double, double, i1)* @f, i64 64, i64 0, i64 32)
  %res = call double %ptr(double %x, double %y, i1 %cond)
  ret double %res
}

define double @tester2(double %x, double %y, i1 %cond) {
entry:
  %ptr = call double (double, double, i1)* (...) @__raptor_truncate_op_func(double (double, double, i1)* @f, i64 64, i64 0, i64 32)
  %res = call double %ptr(double %x, double %y, i1 %cond)
  ret double %res
}

; CHECK: define internal double @__raptor_done_truncate
; TODO
