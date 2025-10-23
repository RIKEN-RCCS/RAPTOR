# RAPTOR

Raptor allows users to easily alter and profile floating-point precision in their C, C++, and Fortran code.
This is achieved using an LLVM pass and an accompanying runtime.


## Dependencies

RAPTOR requires LLVM version 20 with `clang`, `flang`, and `lld` for full capabilities.

In addition, it requires the MPFR library for emulating various floating point precisions.

To install the dependencies on Debian and Ubuntu, [this repository](https://apt.llvm.org/) can be used.
```
sudo apt-get install -y cmake gcc g++ llvm-20-dev libclang-20-dev clang-20 lld-20 mlir-20-tools libmlir-20 libmlir-20-dev libflang-20-dev flang-20 libmpfr-dev
```

LLVM can also be installed using spack as such:
```
spack install llvm+clang+flang+lld+mlir@20
```

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

To run the tests:
``` shell
ninja -C ./build check-all
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
-Rpass=raptor
```

### Compilation (recommended)

RAPTOR installs the three following compiler shims.
```
raptor-clang
raptor-clang++
raptor-flang
```

These can be used in place of `clang`, `clang++`, and `flang` respectively, and they automatically add the required compiler and linker flags.

### Details about required flags

#### Linker flags

Regardless of which configuration RAPTOR is used in, the following flags need to be specified when linking:
``` shell
-lRaptor-RT-$LLVM_VER -lmpfr -lstdc++
```

#### Compilation flags

These flags need to be specified when compiling (the same for flang and clang):

``` shell
-fpass-plugin=$RAPTOR_INSTALL_DIR/lib/LLVMRaptor-$LLVM_VER.so
```


#### LTO compilation flags

Pass the following to the compiler (`clang` or `flang`) when compiling:

``` shell
-flto=full
```

and the following to the compiler when linking if using `clang` or `flang`:

``` shell
-fuse-ld=lld -Wl,--load-pass-plugin=$RAPTOR_INSTALL_DIR/lib/LLDRaptor-$LLVM_VER.so
```

or if using `lld` directly:
``` shell
--load-pass-plugin=$RAPTOR_INSTALL_DIR/lib/LLDRaptor-$LLVM_VER.so
```

### Changes to source code

#### C++

Suppose your original code looks like this:
``` c++
float bar(float a, float b) {
  return a + b;
}
float foo(float *a, float b) {
  a[0] = sqrt(b);
  return bar(a[1], b);
}

  ...
  foo(a, b);
  ...
```

To use RAPTOR to truncate floating-point operations in the call to `foo`, one can replace the call to `foo` with the following:
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


You need to declare the `__raptor_truncate_op_func`, which can be done as such:
``` c++
template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int, int);
template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int);
```

#### Fortran

The usage is analogous to C++.

##### Original code

``` f90
  double precision function simple_sum(a, b) result(c) bind(c)
    implicit none

    double precision, intent(in) :: a, b

    c = a + b

    return
  end function simple_sum
```

``` f90
  c = simple_sum(a, b)
```

##### With RAPTOR

``` f90
  cfty = c_funloc(simple_sum)
  cfty = f__raptor_truncate_op_func(cfty, 64, 1, 10, 4)
  call c_f_procpointer(cfty, ffty)
  c = ffty(a, b)
```

The `f__raptor_truncate_op_func` must be declared:

``` f90
  interface
     function f__raptor_truncate_op_func(tfunc, from_ieee, to_type, to_exponent, to_significand) result (fty) bind (c)
       use iso_c_binding
       implicit none

       integer(c_int), intent(in), value :: from_ieee, to_type, to_exponent, to_significand
       type(c_funptr), intent(in), value :: tfunc
       type(c_funptr) :: fty
     end function f__raptor_truncate_op_func
  end interface
```

See `test/Integration/Truncate/Fortran/simple.f90` for an example.


## Citing RAPTOR

If you use RAPTOR, consider citing the following paper:

``` bibtex
@inproceedings{hoerold_raptor_2025,
  title = {{{RAPTOR}}: {{Numerical Profiling}} of {{Scientific Applications}}},
  booktitle = {Proceedings of the {{International Conference}} for {{High Performance Computing}}, {{Networking}}, {{Storage}} and {{Analysis}}},
  author = {Hoerold, Faveo and Ivanov, Ivan R. and Dhruv, Akash and Moses, William S. and Dubey, Anshu and Wahib, Mohamed and Domke, Jens},
  date = {2025-11},
  series = {{{SC}} '25},
  publisher = {IEEE Press},
}
```
