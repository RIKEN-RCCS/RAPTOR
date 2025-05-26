// RUN: mkdir -p %t
// RUN: %clang -flto=full -O2 -c %S/Inputs/add_func_main.cpp -o %t/add_func_main.o
// RUN: %clang -flto=full -O2 -c %S/Inputs/add_func.cpp -o %t/add_func.o
// RUN: %clang -fuse-ld=lld -flto=full -O2 %t/add_func_main.o %t/add_func.o -o %t/a.out %loadLLDRaptor %linkRaptorRT
