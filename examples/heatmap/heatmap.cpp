// Compile with raptor-clang++ ./heatmap.cpp -O3 -g -o heatmap

#include <iostream>

#include "raptor/raptor.h"

template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int, int);
template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int);
template <typename fty> fty *__raptor_truncate_mem_func(fty *, int, int, int, int);

#define frac 1e-4
#define FROM 64
#define TO 1, 5, 8

double foo (double in) {
    double sum = in * 3.0;  // Exact operation
    double small = in * frac;  // Ok for double, not for truncated
    double cancel = in + small;

    return sum + cancel;
}

int main (int argc, char * argv[]) {

    if (argc != 2) {
        std::cerr << "Expected exactly one floating-point argument." << std::endl;
        exit(1);
    }

    double a = std::stod(argv[1]);

    double trunc = __raptor_expand_mem_value(
        __raptor_truncate_mem_func(foo, FROM, TO)(__raptor_truncate_mem_value(a, FROM, TO)),
        FROM, TO);

    double exact = a * 3.0 + a + a * frac;

    std::cout << "Exact: " << exact << "\n";
    std::cout << "Truncated: " << trunc << std::endl;

    __raptor_fprt_op_dump_status(10);
    std::cout << __raptor_get_trunc_flop_count() << " ops were truncated." << std::endl;

    return 0;
}
