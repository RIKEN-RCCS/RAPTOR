// RUN: if [ %llvmver -ge 12 ] ; then %clang -O2 %s -o %t.a.out %newLoadClangRaptor -mllvm --raptor-truncate-count -lm && %t.a.out | FileCheck %s; fi

#include <cstdio>
#include <cmath>

#define floatty double

// CHECK: FLOP!
// CHECK: FLOP!
// CHECK: FLOP!
// CHECK: FLOP!
// CHECK: FLOP!

extern "C"
void __raptor_fprt_64_52_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc) {
    puts("FLOP!");
}

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

    compute2(A, B, C, N);
    for (int i = 0; i < N; i++)
        C[i] = 0;
    compute(A, B, C, N);
    printf("%f\n", C[5]);
}
