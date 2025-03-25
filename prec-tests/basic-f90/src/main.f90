program test
  implicit none

  integer :: num_args
  character(len=12), dimension(:), allocatable :: args
  real*8 :: a
  real*8 :: b
  real*8 :: c
  
  num_args = command_argument_count()
  allocate(args(num_args))

  call get_command_argument(1, args(1))
  read(args(1), *) a

  call get_command_argument(2, args(2))
  read(args(2), *) b

  c = a + b

  print *, a, ' + ', b, ' = ', c
  
end program test
