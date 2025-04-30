# RAPTOR

Raptor allows users to easily alter and profile floating-point precision in their C, C++, and Fortran code.
This is achieved using an LLVM pass and an accompanying runtime.


## Building

``` shell
cd enzyme
cmake --fresh \
  -DLLVM_DIR="path/to/llvm-install/lib/cmake/llvm" \
  -DCMAKE_INSTALL_PREFIX="path/to/enzyme-install/" \
  -DLLVM_EXTERNAL_LIT="path/to/llvm-project/llvm/utils/lit/lit.py" \
  -B build -G Ninja
ninja -C ./build install
```


## Usage

To use RAPTOR in `clang` (for C and C++) or `flang` (for Fortran), first, the RAPTOR plugin must be loaded, which is done using flags to the compiler.
The flags are identical across the two compilers.

Raptor can be used in either single-file compilation (the default for `clang` and `flang`) or in conjunction with LTO (Link-Time Optimization).

To enable LTO the following flag is needed both when compiling and linking:
``` shell
-flto=full -fuse-ld=lld
```

Also, in order for RAPTOR to work, optimizations must be enabled as such:
``` shell
-O2 # or -O1, or -O3
```

When using the function-scope truncation strategies RAPTOR analyses all transitive function calls and truncates the entire call tree.
Thus, we recommend using LTO in these cases, to enable RAPTOR to have a complete view of your source code.

The following can be used to enable RAPTOR's compilation remarks:
``` shell
-Rpass=enzyme
```


### Usage in LTO

``` shell
-Wl,-mllvm -Wl,-load=$ENZYME_BUILD_DIR/Enzyme/LLDEnzyme-$LLVM_VER.so -L$ENZYME_BUILD_DIR/Enzyme/Runtimes/FPRT/ -lEnzyme-FPRT-Count-$LLVM_VER
```

### Usage in single-file compilation

When compiling:
``` shell
-fplugin=/path/to/Enzyme/enzyme/build/Enzyme/ClangEnzyme-$LLVM_VER.so
```

When linking:
``` shell
-L$ENZYME_BUILD_DIR/Enzyme/Runtimes/FPRT/ -lEnzyme-FPRT-Count-$LLVM_VER
```


### Usage in source code

Suppose your original code looks like this:
``` c++
void bar(float a, flaot b) {
  return a + b;
}
void foo(float *a, float b) {
  a[0] = sqrt(b);
  return bar(a[1], b);
}

  ...
  foo(a, b)
  ...
```

To use RAPTOR to truncate floating-pint operations in the call to `foo`, one can replace the call to `foo` with the following:
``` c++
  ...
  auto f = __enzyme_truncate_op_func(
    /* function */    foo,
    /* from_type */   32,
    /* to_exponent */ 5,
    /* to_mantissa */ 8);
  f(a, b)
  ...
```

At compile time, RAPTOR will replace the call to `__enzyme_truncate_op_func` with a version of `foo` with floating-point operations truncated to the specified precision.
