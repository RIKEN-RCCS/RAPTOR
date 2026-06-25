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
double bar(double a, double b) {
  return a + b;
}
double foo(double *a, double b) {
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
  double precision function simple_sum(a, b) result(c)
    implicit none

    double precision, intent(in) :: a, b

    c = a + b
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

### Mem-mode

Memorization-mode tracks an individual data structure for each floating-point variable or array entry. This makes it a more powerful tool to track floating-point accuracy over time, but also introduces limitations: Only 64-bit floating-point values can currently be truncated using mem-mode. This is because the type must be wide enough to store a pointer to the internal data structure.

#### C++

To make use of mem-mode, one can replace the call to `foo` with the following:
```c++
  ...
  for (double& v : a) { v = __raptor_truncate_mem_value(v, 64, 1, 5, 8); }
  b = __raptor_truncate_mem_value(b, 64, 1, 5, 8);

  auto f = __raptor_truncate_mem_func(
    /* function */    foo,
    /* from_type */   64,
    /* to_type: 0 for builtin IEEE type, 1 for MPFR */ 1,
    /* to_exponent */ 5,
    /* to_mantissa */ 8);
  c = f(a, b);

  for (double& v : a) { v = __raptor_expand_mem_value(v, 64, 1, 5, 8); }
  b = __raptor_expand_mem_value(b, 64, 1, 5, 8);
  c = __raptor_expand_mem_value(c, 64, 1, 5, 8);
```

The functions `__raptor_truncate_mem_value` and `__raptor_expand_mem_value` are used before and after the truncated function call to create and convert to and from the internal data structure.

Similar to op-mode, `__raptor_truncate_mem_func`, `__raptor_truncate_mem_value`, and `__raptor_expand_mem_value` must be declared:

```c++
template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int, int);
template <typename fty> fty *__raptor_truncate_op_func(fty *, int, int, int);
template <typename fty> fty *__raptor_truncate_mem_func(fty *, int, int, int, int);
```

#### Fortran

The usage is analogous to C++:

```f90
use iso_c_binding

...

double precision :: a, b, c;
procedure(simple_sum), pointer :: ffty
type(c_funptr) :: cfty

a = f__raptor_truncate_mem_value(a, 64, 1, 5, 8);
b = f__raptor_truncate_mem_value(b, 64, 1, 5, 8);

cfty = c_funloc(simple_sum)
cfty = f__raptor_truncate_mem_func(cfty, 64, 1, 5, 8)
call c_f_procpointer(cfty, ffty)
c = ffty(a, b)

a = f__raptor_expand_mem_value(a, 64, 1, 5, 8);
b = f__raptor_expand_mem_value(b, 64, 1, 5, 8);
c = f__raptor_expand_mem_value(c, 64, 1, 5, 8);
```

Similar to op-mode, `f__raptor_truncate_mem_func`, `f__raptor_truncate_mem_value`, and `f__raptor_expand_mem_value` must be declared:

```f90
  interface
     function f__raptor_truncate_mem_func(tfunc, from_ieee, &
          to_type, to_exponent, to_significand) result(fty) bind(c)
       use iso_c_binding
       implicit none

       integer(c_int), intent(in), value :: from_ieee, to_type, to_exponent, to_significand
       type(c_funptr), intent(in), value :: tfunc
       type(c_funptr) :: fty
     end function f__raptor_truncate_mem_func
     double precision function f__raptor_truncate_mem_value(value, from_ieee, &
          to_type, to_exponent, to_significand) result(ptr) bind(c)
       use iso_c_binding
       implicit none

       real(c_double), intent(in), value :: value
       integer(c_int), intent(in), value :: from_ieee, to_type, to_exponent, to_significand
     end function f__raptor_truncate_mem_value
     double precision function f__raptor_expand_mem_value(ptr, from_ieee, &
          to_type, to_exponent, to_significand) result(value) bind(c)
       use iso_c_binding
       implicit none

       real(c_double), intent(in), value :: ptr
       integer(c_int), intent(in), value :: from_ieee, to_type, to_exponent, to_significand
     end function f__raptor_expand_mem_value
  end interface
```

## Citing RAPTOR

If you use RAPTOR, consider citing the following paper:

``` bibtex
@inproceedings{10.1145/3712285.3759810,
author = {Hoerold, Faveo and Ivanov, Ivan R. and Dhruv, Akash and Moses, William S. and Dubey, Anshu and Wahib, Mohamed and Domke, Jens},
title = {RAPTOR: Practical Numerical Profiling of Scientific Applications},
year = {2025},
isbn = {9798400714665},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3712285.3759810},
doi = {10.1145/3712285.3759810},
abstract = {The proliferation of low-precision units in modern high-performance architectures increasingly burdens domain scientists. Historically, the choice in HPC was easy: can we get away with 32&nbsp;bit floating-point operations and lower bandwidth requirements, or is FP64 necessary? Driven by Artificial Intelligence, vendors introduce novel low-precision units for vector and tensor operations, and FP64 capabilities stagnate or are reduced. This forces scientists to re-evaluate their codes, but a trivial search-and-replace approach to go from FP64 to FP16 will not suffice.We introduce RAPTOR: a numerical profiling tool to guide scientists in their search for code regions where precision lowering is feasible. Using LLVM, we transparently replace high-precision computations using low-precision units, or emulate a user-defined precision. RAPTOR is a novel, feature-rich approach—with focus on ease of use—to change, profile, and reason about numerical requirements and instabilities, which we demonstrate with four real-world multi-physics Flash-X applications.},
booktitle = {Proceedings of the International Conference for High Performance Computing, Networking, Storage and Analysis},
pages = {661–680},
numpages = {20},
keywords = {Mixed precision, low precision, numerical profiling, error tracking, simulation accuracy, multiphysics, LLVM, MPFR},
location = {
},
series = {SC '25}
}
```
