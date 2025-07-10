// RUN: %clang -O3 %s -o %t.a.out %loadClangRaptor %linkRaptorRT %includeRaptorRT -lm && RAPTOR_FLOP_LOG_PREFIX=%t.flop_log %t.a.out && xxd %t.flop_log.double | FileCheck %s

// CHECK: 00000000: 0000 0000 0000 f03f 0000 0000 0000 0040
// CHECK: 00000010: 0000 0000 0000 0840 0000 0000 0000 0040
// CHECK: 00000020: 0000 0000 0000 0840 0000 0000 0000 1040
// CHECK: 00000030: 0000 0000 0000 1c40 0000 0000 0000 0040

#include "raptor/raptor.h"
#include <cstdio>

double simple_add(double a, double b) {
    return 2 * (a + b);
    // TODO float and half
    // return a + b + ((float)a + (float)b);
}

template <typename fty> fty *__raptor_log_flops(fty *);

int main() {
    double trunc;

    trunc = __raptor_log_flops(simple_add)(1, 2);
    printf("A1 %f\n", trunc);
    trunc = __raptor_log_flops(simple_add)(3, 4);
    printf("A2 %f\n", trunc);
    __raptor_clear_flop_log_double();
    trunc = __raptor_log_flops(simple_add)(5, 6);
    printf("A3 %f\n", trunc);

    return 0;
}
