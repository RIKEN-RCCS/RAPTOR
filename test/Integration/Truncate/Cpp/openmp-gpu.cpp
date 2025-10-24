// clang-format off
// RUN: %clang -O3          %s -o %t.a.out %loadClangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out
// RUN: %clang -O3 -fopenmp %s -o %t.a.out %loadClangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out
// RUN: if [ "%hasOpenMPGPU" == "1" ]; then %clang -O3 -fopenmp --offload-arch=native -nogpulib %s -o %t.a.out %loadClangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out; fi
// clang-format on

#include "../../test_utils.h"
#include <cstdio>

#define FROM 64
#define TO 0, 16

template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int);

double kernel(double a, double b) { return a + b; }

int main() {
  double a = 1;
  double b = 10000;
  double c;
#pragma omp target map(tofrom : c)
  {
    c = __raptor_truncate_op_func(kernel, FROM, TO)(a, b);
  }

  printf("%f + %f = %f\n", a, b, c);
  APPROX_EQ(c, b, 1e-5);
  return 0;
}
