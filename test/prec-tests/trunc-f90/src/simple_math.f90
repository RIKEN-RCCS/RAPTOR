      module simple_math
      implicit none

      private

      public :: simple_sum

      contains

      double precision function simple_sum(a, b) result(c)
      implicit none

      double precision, intent(in) :: a, b

      c = a + b

      return
      end function simple_sum

      end module simple_math
