// clang-format off
// RUN: %clang -O2 %s -o %t.a.out %linkRaptorRT %loadClangPluginRaptor -mllvm --raptor-truncate-count -lm && %t.a.out

#include <cstdio>
#include <cmath>

#include "../../test_utils.h"

#define floatty double
#define FROM 64
#define TO 1, 8, 23

template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int, int);

// CHECK: FLOP!
// CHECK: FLOP!
// CHECK: FLOP!
// CHECK: FLOP!
// CHECK: FLOP!

extern "C" long long __raptor_get_double_flop_count();
extern "C" long long __raptor_get_trunc_flop_count();

#define N 10

__attribute__((noinline))
floatty simple_add(floatty a, floatty b) {
    return a + b;
}
__attribute__((noinline))
floatty intrinsics(floatty a, floatty b) {
    return sqrt(a) * pow(b, 2);
}
__attribute__((noinline)) static
floatty intrinsics2(floatty a, floatty b) {
    return sin(a) * cos(b);
}
__attribute__((noinline))
floatty compute2(floatty *A, floatty *B, floatty *C, int n) {
    for (int i = 0; i < n; i++) {
        C[i] = A[i] / 2 + intrinsics2(A[i], simple_add(B[i] * 10000, 0.000001));
    }
    return C[0];
}
__attribute__((noinline))
floatty compute(floatty *A, floatty *B, floatty *C, int n) {
    for (int i = 0; i < n; i++) {
        C[i] = A[i] / 2 + intrinsics(A[i], simple_add(B[i] * 10000, 0.000001));
    }
    return C[0];
}

int main() {
    floatty A[N];
    floatty B[N];
    floatty C[N];

    for (int i = 0; i < N; i++) {
        A[i] = 1 + i % 5;
        B[i] = 1 + i % 3;
    }

    TEST_EQ(__raptor_get_double_flop_count(), 0);
    TEST_EQ(__raptor_get_trunc_flop_count(), 0);

    compute2(A, B, C, N);

    TEST_EQ(__raptor_get_double_flop_count(), 70);
    TEST_EQ(__raptor_get_trunc_flop_count(), 0);

    for (int i = 0; i < N; i++)
        C[i] = 0;

    compute(A, B, C, N);

    TEST_EQ(__raptor_get_double_flop_count(), 140);
    TEST_EQ(__raptor_get_trunc_flop_count(), 0);


    for (int i = 0; i < N; i++)
        C[i] = 0;

    __raptor_truncate_op_func(compute, FROM, TO)(A, B, C, N);

    TEST_EQ(__raptor_get_double_flop_count(), 140);
    TEST_EQ(__raptor_get_trunc_flop_count(), 70);


}
