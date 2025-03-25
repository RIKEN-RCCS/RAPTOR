#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int main (int argc, char *argv[]) {
    double a = strtod(argv[1], &argv[1]+strlen(argv[1]));
    double b = strtod(argv[2], &argv[2]+strlen(argv[2]));

    double c = a + b;
    
    printf("%.20f + %.20f = %.20f\n", a, b, c);
    
    return 0;
}
