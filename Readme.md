# RAPTOR

Raptor allows users to easily alter and profile floating-point precision in their C, C++, and Fortran code.
This is achieved using an LLVM pass and an accompanying runtime.


## Dependencies

RAPTOR requires LLVM version 20 with clang and flang support for C/C++ and Fortran respectively.

## Building

``` shell
export LLVM_INSTALL_DIR="<path-to-llvm-install-dir>"
export RAPTOR_INSTALL_DIR="<path-to-raptor-install-dir>"
cmake --fresh \
  -DLLVM_DIR="$LLVM_INSTALL_DIR/lib/cmake/llvm" \
  -DCMAKE_INSTALL_PREFIX="$RAPTOR_INSTALL_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -B build -G Ninja
ninja -C ./build install
```

Running the tests requires either specifying an LLVM build directory instead of an install one, or specifying the lit path using `-DLLVM_EXTERNAL_LIT="path/to/llvm-project/llvm/utils/lit/lit.py"`.

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
-Rpass=raptor
```


### Usage in LTO

``` shell
-Wl,-mllvm -Wl,-load=$RAPTOR_INSTALL_DIR/lib/LLDRaptor-$LLVM_VER.so -L$RAPTOR_INSTALL_DIR/lib/ -lRaptor-RT-$LLVM_VER
```

### Usage in single-file compilation

When compiling:
``` shell
-fplugin=$RAPTOR_INSTALL_DIR/lib/ClangRaptor-$LLVM_VER.so
```

When linking:
``` shell
-L$RAPTOR_INSTALL_DIR/lib/FPRT/ -lRaptor-RT-$LLVM_VER
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
  auto f = __raptor_truncate_op_func(
    /* function */    foo,
    /* from_type */   32,
    /* to_type: 0 for builtin IEEE type, 1 for MPFR */   1,
    /* to_exponent */ 5,
    /* to_mantissa */ 8);
  f(a, b)
  ...
```

``` c++
  ...
  auto f = __raptor_truncate_op_func(
    /* function */    foo,
    /* from_type */   64,
    /* to_type: 0 for builtin IEEE type, 1 for MPFR */   0,
    /* to_width */ 32);
  f(a, b)
  ...
```

At compile time, RAPTOR will replace the call to `__raptor_truncate_op_func` with a version of `foo` with floating-point operations truncated to the specified precision.
