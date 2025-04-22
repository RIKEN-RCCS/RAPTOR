module truncate
  use simple_math, only: simple_sum

  implicit none

  interface
     function f_enzyme_get_trunc_flop_count() result(count) bind(C)
       use, intrinsic :: iso_c_binding, only: c_long_long
       implicit none

       integer(c_long_long) :: count
     end function
  end interface

  interface
     function f_enzyme_get_double_flop_count() result(count) bind(C)
       use, intrinsic :: iso_c_binding, only: c_long_long
       implicit none

       integer(c_long_long) :: count
     end function
  end interface

  interface
     function f_enzyme_get_float_flop_count() result(count) bind(C)
       use, intrinsic :: iso_c_binding, only: c_long_long
       implicit none

       integer(c_long_long) :: count
     end function
  end interface

  interface
     function f_enzyme_get_half_flop_count() result(count) bind(C)
       use, intrinsic :: iso_c_binding, only: c_long_long
       implicit none

       integer(c_long_long) :: count
     end function
  end interface

  public :: simple_sum
  public :: f__enzyme_truncate_op_func_simple_sum
contains
  ! function simple_sum(a, b) result(c)
  !   double precision, intent(in) :: a, b
  !   double precision :: c

  !   c = a + b
  ! end function simple_sum

  function f__enzyme_truncate_op_func_simple_sum(from, to_e, to_m, a, b) result(c)
    implicit none

    integer :: from, to_e, to_m
    double precision, intent(in) :: a, b
    double precision :: c

    c = simple_sum(a, b)
  end function f__enzyme_truncate_op_func_simple_sum
end module truncate

! module simple_math
!   implicit none

!   public :: simple_sum

!   contains

!   function simple_sum(a, b) result(c)
!     double precision, intent(in) :: a, b
!     double precision :: c

!     c = a + b
!   end function simple_sum
! end module simple_math

program trunc_f90
  use truncate
  ! use simple_math
  implicit none

  double precision :: a, b, c

  character(len=12), dimension(2) :: args

  call get_command_argument(1, args(1))
  call get_command_argument(2, args(2))

  read(args(1), *) a
  read(args(2), *) b

  c = f__enzyme_truncate_op_func_simple_sum(64, 3, 4, a, b)

  write(*,*) a, "+", b, "=", a+b
  write(*,*) a, "+", b, "=", c

  write(*,*) "Number of truncated flops: ", f_enzyme_get_trunc_flop_count()
  write(*,*) "Number of double flops: ", f_enzyme_get_double_flop_count()
  write(*,*) "Number of float flops: ", f_enzyme_get_float_flop_count()
  write(*,*) "Number of half flops: ", f_enzyme_get_half_flop_count()

! contains
!   function f__enzyme_truncate_op_func_simple_sum(from, to_e, to_m, a, b) result(c)
!     use simple_math
!     implicit none

!     integer :: from, to_e, to_m
!     double precision, intent(in) :: a, b
!     double precision :: c

!     c = simple_sum(a, b)
!   end function f__enzyme_truncate_op_func_simple_sum
end program trunc_f90
