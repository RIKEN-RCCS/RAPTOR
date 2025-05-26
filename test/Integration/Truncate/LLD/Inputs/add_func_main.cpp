
#include <type_traits>

extern "C" int printf (const char *__format, ...);
extern double add_func(double a, double b);
extern std::add_pointer<decltype(add_func)>::type __raptor_truncate_op_func(...);

int main(int argc, char **argv) {
    double a = 1;
    double b = 1000;
    printf("%f + %f = %f", a, b, __raptor_truncate_op_func(add_func, 64, 1, 5, 5)(a, b));
}
