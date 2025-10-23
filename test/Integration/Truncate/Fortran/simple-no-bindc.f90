! ! Circumvent tests in versions where LLVM ships with a non-functional flang
! ! (see https://github.com/llvm/llvm-project/issues/138340 )
! XFAIL: llvm-major:{{21|22}}
! RUN: %flang -O1 %s -o %t.a.out %loadFlangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out 100000 2 | FileCheck %s
! RUN: %flang -O2 %s -o %t.a.out %loadFlangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out 100000 2 | FileCheck %s
! RUN: %flang -O3 %s -o %t.a.out %loadFlangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out 100000 2 | FileCheck %s
! RUN: %flang -g -O3 %s -o %t.a.out %loadFlangRaptor %linkRaptorRT -lm -lmpfr && %t.a.out 100000 2 | FileCheck %s

! CHECK: 100000. + 2. = 98304.

module simple_math
  implicit none
contains

  double precision function simple_sum(a, b) result(c)
    implicit none

    double precision, intent(in) :: a, b

    c = a + b

    return
  end function simple_sum
end module simple_math

program trunc_f90
  use iso_c_binding

  !use truncate
  use simple_math, only: simple_sum

  implicit none

  double precision:: a, b, c

  character(len=12), dimension(2) :: args

  interface
     function f__raptor_truncate_op_func(tfunc, from_ieee, to_type, to_exponent, to_significand) result (fty) bind (c)
       use iso_c_binding
       implicit none

       integer(c_int), intent(in), value :: from_ieee, to_type, to_exponent, to_significand
       type(c_funptr), intent(in), value :: tfunc
       type(c_funptr) :: fty
     end function f__raptor_truncate_op_func
  end interface

  procedure(simple_sum), pointer :: ffty
  type(c_funptr) :: cfty

  call get_command_argument(1, args(1))
  call get_command_argument(2, args(2))
  read(args(1), *) a
  read(args(2), *) b

  cfty = c_funloc(simple_sum)
  cfty = f__raptor_truncate_op_func(cfty, 64, 1, 10, 4)
  call c_f_procpointer(cfty, ffty)
  c = ffty(a, b)

  write(*,*) a, "+", b, "=", c
end program trunc_f90
