#include <iostream>
#include <iomanip>
#include <string>

#include <functional>

// Enzyme
#ifdef LTO_TRUNC
#include "enzyme/fprt/fprt.h"
#define FROM 64
#define TO 2
template <typename fty> fty *__enzyme_truncate_mem_func(fty *, int, int);
template <typename fty> fty *__enzyme_truncate_op_func(fty *, int, int, int);
#endif

double enzyme_add(double a, double b) {
  return a + b;
}

double nest(double a, double b) {
  std::function f = enzyme_add;
  
  return f(a, b);
}

int main(int argc, char *argv[]) {
  double a, b, c;
    
  a = std::stod(argv[1]);
  b = std::stod(argv[2]);

#ifdef LTO_TRUNC
  c = __enzyme_truncate_op_func(nest, FROM, 0, TO)(a, b);
#else
  c = nest(a, b);
#endif

  std::cout << std::fixed << std::setprecision(20);
  std::cout << a << " + " << b << " = " << c << std::endl;
    
  return 0;
}
