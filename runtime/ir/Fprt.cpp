#include <atomic>
#include <mpfr.h>

#include <raptor/Common.h>

// Global variable to count truncated flops
// TODO only implemented for op mode at the moment
// extern std::atomic<long long> trunc_flop_counter;
// extern std::atomic<long long> trunc_excl_flop_counter;
// extern std::atomic<long long> double_flop_counter;
// extern std::atomic<long long> float_flop_counter;
// extern std::atomic<long long> half_flop_counter;

std::atomic<long long> shadow_err_counter = 0;
