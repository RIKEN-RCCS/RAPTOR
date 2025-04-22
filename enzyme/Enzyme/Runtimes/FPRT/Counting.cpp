
#include <enzyme/fprt/fprt.h>
#include <enzyme/fprt/mpfr.h>

#include <vector>
#include <algorithm>

#include <mpi.h>

// Global variable to count truncated flops
// TODO only implemented for op mode at the moment
std::atomic<long long> trunc_flop_counter = 0;
std::atomic<long long> double_flop_counter = 0;
std::atomic<long long> float_flop_counter = 0;
std::atomic<long long> half_flop_counter = 0;

std::atomic<long long> trunc_load_counter = 0;
std::atomic<long long> trunc_store_counter = 0;
std::atomic<long long> original_load_counter = 0;
std::atomic<long long> original_store_counter = 0;

// TODO this needs to be thread local
std::atomic<bool> global_is_truncating = false;

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_trunc_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return trunc_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_double_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return double_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_float_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return float_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_half_flop_count() {
  if (trunc_flop_counter < 0) {
    puts("ERROR: FLOP Counter Overflow!");
    exit(0);
  }

  return half_flop_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_trunc_flop_count() {
  return __enzyme_get_trunc_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_double_flop_count() {
  return __enzyme_get_double_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_float_flop_count() {
  return __enzyme_get_float_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_half_flop_count() {
  return __enzyme_get_half_flop_count();
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_trunc_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
  trunc_flop_counter.fetch_add(1,  std::memory_order_relaxed);
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
  double_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_32_23_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
  float_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_16_10_count(int64_t exponent, int64_t significand,
                               int64_t mode, const char *loc, mpfr_t *scratch) {
  half_flop_counter.fetch_add(1, std::memory_order_relaxed);
}

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_reset_shadow_trace() {
  long long ret = shadow_err_counter;
  shadow_err_counter = 0;
  return ret;
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_reset_shadow_trace() {
  return __enzyme_reset_shadow_trace();
}


bool __op_dump_cmp(std::pair<const char *, __enzyme_op>& a,
                   std::pair<const char *, __enzyme_op>& b) {
  return a.second.count_thresh > b.second.count_thresh;
}

__ENZYME_MPFR_ATTRIBUTES
void enzyme_fprt_op_dump_status(int num) {
  int size, rank;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (opdata.size() < num) num = opdata.size();

  if (rank == 0) {
    std::cerr << "Information about top " << num
              << " operations." << std::endl;
  }
  // std::vector<unsigned long long> key_recvcounts(size);
  // std::vector<unsigned long long> key_displs(size);
  // std::vector<unsigned long long> char_recvcounts(size);
  // std::vector<unsigned long long> char_displs(size);
  // std::vector<char> key_chars;
  // std::vector<char> key_sizes;

  std::vector<std::pair<const char *, struct __enzyme_op>> od_vec;
  std::vector<double> l1_vec;
  std::vector<long long> ct_vec, c_vec;

  // // Synchronize keys between processes.
  // // Build explicit char vector of keys.
  // // Collect size of each individual key.
  // for (auto& it : opdata) {
  //   int sz = 0;
  //   for (char* c = it.first, *c, ++c) {
  //     key_chars.push_back(c);
  //     ++sz;
  //   }
  //   key_sizes.push_back(sz);
  // }
  // assert(op_data.size() == key_sizes.size());

  // key_recvcounts[rank] = op_data.size();
  // char_counts[rank] = key_chars.size();

  // MPI_Allgather(MPI_IN_PLACE, 0, NULL,
  //               key_counts.data(), 1, MPI_UNSIGNED_LONG_LONG,
  //               MPI_COMM_WORLD);
  // MPI_Allgather(MPI_IN_PLACE, 0, NULL,
  //               char_counts.data(), 1, MPI_UNSIGNED_LONG_LONG,
  //               MPI_COMM_WORLD);

  // unsigned long long key_recvcounts_sum = 0;
  // unsigned long long char_recvcounts_sum = 0;
  // for (unsigned long long i = 0; i < size; ++i) {
  //   key_displs[i] = key_recvcounts_sum;
  //   char_displs[i] = char_recvcounts_sum;
  //   key_recvcounts_sum += key_recvcounts[i];
  //   char_recvcounts_sum += char_recvcounts[i];
  // }
  // std::vector<unsigned long long> key_sizes_recv(key_recvcount_sum);
  // std::vector<char> key_chars_recv(char_recvcounts_sum);

  // MPI_Allgatherv(key_sizes.data(), key_sizes.size(), MPI_UNSIGNED_LONG_LONG,
  //                key_sizes_recv.data(), key_recvcounts.data(), key_displs.data(),
  //                MPI_UNSIGNED_LONG_LONG, MPI_COMM_WORLD);
  // MPI_Allgatherv(key_chars.data(), key_chars.size(), MPI_CHAR,
  //                key_chars_recv.data(), char_recvcounts.data(), char_displs.data(),
  //                MPI_CHAR, MPI_COMM_WORLD);

  // // Build strings
  // std::vector<std::string> keys;
  // unsigned long long char_idx = 0;
  // for (unsigned long long i = 0; i < key_recvcount_sum; ++i) {
  //   keys.push_back(std::string(key_chars[char_idx], key_sizes[i]));
  //   char_idx += key_sizes[i];
  // }

  // // Make sure every key is represented in local opdata map
  // for (auto& key : keys) {
  //   opdata.insert(key, struct __enzyme_op{"FILL", 0, 0, 0});
  // }

  // The order of iteration over keys will be the same on all processes.
  for (auto& it : opdata) {
    od_vec.push_back(it);
    l1_vec.push_back(it.second.l1_err);
    ct_vec.push_back(it.second.count_thresh);
    c_vec.push_back(it.second.count);
  }

  // Perform an allreduce over opdata elements stored in the vector.
  // if (rank == 0) {
  //   MPI_Reduce(MPI_IN_PLACE, l1_vec.data(), od_vec.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  //   MPI_Reduce(MPI_IN_PLACE, ct_vec.data(), od_vec.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  //   MPI_Reduce(MPI_IN_PLACE,  c_vec.data(), od_vec.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  // } else {
  //   MPI_Reduce(l1_vec.data(), NULL, od_vec.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  //   MPI_Reduce(ct_vec.data(), NULL, od_vec.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  //   MPI_Reduce( c_vec.data(), NULL, od_vec.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  // }

  if (rank == 0) {
    // for (int i = 0; i < od_vec.size(); ++i) {
    //   od_vec[i].second.l1_err = l1_vec[i];
    //   od_vec[i].second.count_thresh = ct_vec[i];
    //   od_vec[i].second.count = c_vec[i];
    // }

    std::sort(od_vec.begin(), od_vec.end(), __op_dump_cmp);

    for (auto it = od_vec.begin(); it != od_vec.begin() + num; ++it) {
      std::cout << it->first << ": " << it->second.count << "x" << it->second.op
                << " L1 Error Norm: " << it->second.l1_err
                << " Number of violations: " << it->second.count_thresh
                << " Ignored " << it->second.count_ignore << " times."
                << std::endl;
    }
  }
}

long long __enzyme_get_memory_access_trunc_store() {
  return trunc_store_counter;
}
__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_memory_access_trunc_load() { return trunc_load_counter; }

__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_memory_access_original_store() {
  return original_store_counter;
}
__ENZYME_MPFR_ATTRIBUTES
long long __enzyme_get_memory_access_original_load() {
  return original_load_counter;
}

__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_memory_access_trunc_store() {
  return __enzyme_get_memory_access_trunc_store();
}
__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_memory_access_trunc_load() {
  return __enzyme_get_memory_access_trunc_load();
}
__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_memory_access_original_store() {
  return __enzyme_get_memory_access_original_store();
}
__ENZYME_MPFR_ATTRIBUTES
long long f_enzyme_get_memory_access_original_load() {
  return __enzyme_get_memory_access_original_load();
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_memory_access(void *ptr, int64_t size, int64_t is_store) {
  if (global_is_truncating) {
    if (is_store)
      trunc_store_counter.fetch_add(size, std::memory_order_relaxed);
    else
      trunc_load_counter.fetch_add(size, std::memory_order_relaxed);
  } else {
    if (is_store)
      original_store_counter.fetch_add(size, std::memory_order_relaxed);
    else
      original_load_counter.fetch_add(size, std::memory_order_relaxed);
  }
}

__ENZYME_MPFR_ATTRIBUTES
void enzyme_fprt_op_clear() {
  opdata.clear();
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_trunc_change(int64_t is_truncating,
                                int64_t to_e,
                                int64_t to_m,
                                int64_t mode) {
  global_is_truncating.store(is_truncating);

  // If we are starting to truncate, set the max and min exponents
  // Can't do it for mem mode currently because we may have truncated variables
  // with unsupported exponent lengths, and those would result in undefined
  // behaviour.
  if (is_truncating && __enzyme_fprt_is_op_mode(mode)) {
    // TODO we need a stack if we want to support nested truncations
    // int64_t max_e = 1 << (to_e - 1);
    // int64_t min_e = -max_e - to_m;
    // https://www.mpfr.org/mpfr-current/mpfr.html
    // https://stackoverflow.com/questions/38664778/subnormal-numbers-in-different-precisions-with-mpfr
    int64_t max_e = 1 << (to_e - 1);
    int64_t min_e = -max_e + 2 - to_m + 2;
    mpfr_set_emax(max_e);
    mpfr_set_emin(min_e);
  }
}

__ENZYME_MPFR_ATTRIBUTES
void *__enzyme_fprt_64_52_get_scratch(int64_t to_e, int64_t to_m, int64_t mode,
                                      const char *loc, void *scratch) {
  mpfr_t *mem = (mpfr_t *)malloc(sizeof(mem[0]) * MAX_MPFR_OPERANDS);
  for (unsigned i = 0; i < MAX_MPFR_OPERANDS; i++)
    mpfr_init2(mem[i], to_m);
  return mem;
}

__ENZYME_MPFR_ATTRIBUTES
void __enzyme_fprt_64_52_free_scratch(int64_t to_e, int64_t to_m, int64_t mode,
                                      const char *loc, void *scratch) {
  mpfr_t *mem = (mpfr_t *)scratch;
  for (unsigned i = 0; i < MAX_MPFR_OPERANDS; i++)
    mpfr_clear(mem[i]);
  free(mem);
}
