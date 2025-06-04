// clang-format off

// RUN: %clang -O3          %s -o %t.a.out %loadClangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out
// RUN: %clang -O3 -fopenmp %s -o %t.a.out %loadClangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out

// CHECK: 1.000000 + 1000.000000 = 1000.000000

// clang-format on

#include "../../test_utils.h"
#include <cstdio>

#define FROM 64
#define TO 1, 10, 8

template <typename fty>
fty *__raptor_truncate_op_func(fty *, int, int, int, int);

double par_for(double a, double b) {
  double c = 0;
#pragma omp parallel for
  for (int i = 0; i < 1; i++) {
    c = a + b;
  }
  return c;
}

double teams(double a, double b) {
  double c = 0;
#pragma omp teams
  {
    c = a + b;
  }
  return c;
}

double teams_par(double a, double b) {
  double c = 0;
#pragma omp teams parallel
  {
    c = a + b;
  }
  return c;
}

double teams__par(double a, double b) {
  double c = 0;
#pragma omp teams
  {
#pragma omp parallel
    {
      c = a + b;
    }
  }
  return c;
}

double par(double a, double b) {
  double c = 0;
#pragma omp parallel
  {
    c = a + b;
  }
  return c;
}

int main() {
  double a = 1;
  double b = 1000;
  double c;
  c = __raptor_truncate_op_func(par, FROM, TO)(a, b);
  printf("%f + %f = %f\n", a, b, c);
  APPROX_EQ(c, 1000, 1e-5);

  c = __raptor_truncate_op_func(par_for, FROM, TO)(a, b);
  printf("%f + %f = %f\n", a, b, c);
  APPROX_EQ(c, 1000, 1e-5);

  c = __raptor_truncate_op_func(teams, FROM, TO)(a, b);
  printf("%f + %f = %f\n", a, b, c);
  APPROX_EQ(c, 1000, 1e-5);

  c = __raptor_truncate_op_func(teams_par, FROM, TO)(a, b);
  printf("%f + %f = %f\n", a, b, c);
  APPROX_EQ(c, 1000, 1e-5);

  c = __raptor_truncate_op_func(teams__par, FROM, TO)(a, b);
  printf("%f + %f = %f\n", a, b, c);
  APPROX_EQ(c, 1000, 1e-5);

  return 0;
}
