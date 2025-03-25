#include <iostream>
#include <iomanip>
#include <string>

// Enzyme
// #include "enzyme/fprt/fprt.h"
// #define FROM 64
// #define TO 2
// template <typename fty> fty *__enzyme_truncate_mem_func(fty *, int, int);
// template <typename fty> fty *__enzyme_truncate_op_func(fty *, int, int, int);

double enzyme_add(double a, double b) {
  return a + b;
}

int main(int argc, char *argv[]) {
  double a, b, c;
    
  a = std::stod(argv[1]);
  b = std::stod(argv[2]);
    
  //c = __enzyme_truncate_op_func(enzyme_add, FROM, 0, TO)(a, b);
  c = enzyme_add(a, b);

  std::cout << std::fixed << std::setprecision(20);
  std::cout << a << " + " << b << " = " << c << std::endl;
    
  return 0;
}
