#include <iostream>
#include <iomanip>
#include <string>

int main(int argc, char *argv[]) {
    double a, b;

    a = std::stod(argv[1]);
    b = std::stod(argv[2]);

    double c = a + b;

    std::cout << std::fixed << std::setprecision(20);
    std::cout << a << " + " << b << " = " << c << std::endl;

    return 0;
}
