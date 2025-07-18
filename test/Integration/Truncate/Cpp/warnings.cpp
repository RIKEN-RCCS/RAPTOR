// clang-format off
// RUN: %clang -c -DTRUNC_MEM -O2    %s -o /dev/null -emit-llvm %loadClangRaptor -Xclang -verify -Rpass=raptor
// RUN: %clang -c -DTRUNC_MEM -O2 -g %s -o /dev/null -emit-llvm %loadClangRaptor -Xclang -verify -Rpass=raptor
// COM: %clang -c -DTRUNC_OP  -O2    %s -o /dev/null -emit-llvm %loadClangRaptor -Xclang -verify -Rpass=raptor
// COM: %clang -c -DTRUNC_OP  -O2 -g %s -o /dev/null -emit-llvm %loadClangRaptor -Xclang -verify -Rpass=raptor

#include <math.h>
#include <stdio.h>

#define FROM 64
#define TO 1, 8, 32

double bithack(double a) {
  return *((int64_t *)&a) + 1; // expected-remark {{Will not follow FP through this cast.}}, expected-remark {{Will not follow FP through this cast.}}
}
__attribute__((noinline)) void print_d(double a) {
  printf("%f\n", a); // expected-remark {{Will not follow FP through this function call as the definition is not available.}}
}
__attribute__((noinline)) float truncf(double a) {
  return (float)a; // expected-remark {{Will not follow FP through this cast.}}
}

double intrinsics(double a, double b) {
  return bithack(a) *
         truncf(b); // expected-remark {{Will not follow FP through this cast.}}
}

typedef double (*fty)(double *, double *, double *, int);

typedef double (*fty2)(double, double);

template <typename fty>
fty *__raptor_truncate_mem_func(fty *, int, int, int, int);
extern fty __raptor_truncate_op_func_2(...);
extern fty2 __raptor_truncate_op_func(...);
extern double __raptor_truncate_mem_value(...);
extern double __raptor_expand_mem_value(...);

int main() {
#ifdef TRUNC_MEM
  {
    double a = 2;
    double b = 3;
    a = __raptor_truncate_mem_value(a, FROM, TO);
    b = __raptor_truncate_mem_value(b, FROM, TO);
    double trunc = __raptor_expand_mem_value(
        __raptor_truncate_mem_func(intrinsics, FROM, TO)(a, b), FROM, TO);
  }
  {
    double a = 2;
    a = __raptor_truncate_mem_value(a, FROM, TO);
    __raptor_truncate_mem_func(print_d, FROM, TO)(a);
  }
#endif
#ifdef TRUNC_OP
  {
    double a = 2;
    double b = 3;
    double trunc = __raptor_truncate_op_func(intrinsics, FROM, TO)(a, b);
  }
#endif
}
