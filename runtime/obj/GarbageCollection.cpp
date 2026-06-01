//===- Trace.cpp - FLOP Garbage collection wrappers
//---------------------------------===//
//
//                             Raptor Project
//
// Part of the Raptor Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// If using this code in an academic setting, please cite the following:
// @incollection{raptorNeurips,
// title = {Instead of Rewriting Foreign Code for Machine Learning,
//          Automatically Synthesize Fast Gradients},
// author = {Moses, William S. and Churavy, Valentin},
// booktitle = {Advances in Neural Information Processing Systems 33},
// year = {2020},
// note = {To appear in},
// }
//
//===----------------------------------------------------------------------===//
//
// This file contains infrastructure for flop tracing
//
// It is implemented as a .cpp file and not as a header becaues we want to use
// C++ features and still be able to use it in C code.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <list>
#include <type_traits>
#include <vector>
#include <mpfr.h>
#include <stdint.h>
#include <stdlib.h>

#define RAPTOR_FPRT_ENABLE_GARBAGE_COLLECTION
#define RAPTOR_FPRT_ENABLE_SHADOW_RESIDUALS

#include <raptor/Common.h>
#include <raptor/raptor.h>

bool excl_trunc = false;

namespace gcfloatidmap {
  /* Define type constraints to enable compile time checks */
  // Constraints for type T that do not use id map (address is used as id)
  template <typename T>
  using valid_no_id_t = std::enable_if_t<
    std::is_floating_point_v<T> && // Is native floating point type
    // Not dealing floating point sizes larger than pointer size for now
    (sizeof(T) == sizeof(__raptor_fp *)), bool>;
  // Constraints for type T that uses id map
  template <typename T>
  using valid_use_id_t = std::enable_if_t<
    std::is_floating_point_v<T> && // Is native floating point type
    // Floating point size is smaller than pointer size
    (sizeof(T) < sizeof(__raptor_fp *)) && 
    // Sanity check: should be 32- or 16-bit
    (sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint16_t)), bool>;
  // Corresponding unsigned int type used for indexing for type T that uses id
  template <typename T, valid_use_id_t<T> = true>
  using fp_to_uint_t = std::enable_if_t<
    // Sanity check: as of C++23, the smallest native fp types size is 16 bits
    (sizeof(T) >= sizeof(uint16_t)),
    // size_t is only guaranteed to be at least 16-bit wide per C/C++ standard
    std::conditional_t<sizeof(T) == sizeof(size_t), size_t,
    std::conditional_t<sizeof(T) == sizeof(uint32_t), uint32_t,
    uint16_t>>>;
  // Constraints for type T that uses id with corresponding index type U
  template <typename T, typename U>
  using use_id_t = std::enable_if_t<std::is_same_v<fp_to_uint_t<T>, U>, bool>;

  /* Define types that help to calculate compile-time constants */
  // Whether type T uses id, default with value false
  template<typename T, typename U = bool> 
  struct is_use_id_t : std::false_type {};
  // Has value true if type T uses id
  template<typename T>
  struct is_use_id_t<T, valid_use_id_t<T>> : std::true_type {};
  // Get the index of type U that uses id among types that uses id in T...
  template<typename U, unsigned I, bool B, typename ...T>
  struct get_index {};
  // U is not found, check if it is next type V, increment I only if V uses id
  template<typename U, unsigned I, typename V, typename ...T>
  struct get_index<U, I, false, V, T...> : get_index<U, I + std::conditional_t<
    is_use_id_t<V>::value, std::integral_constant<unsigned, 1>,
    std::integral_constant<unsigned, 0>>::value, std::is_same_v<U,V>, T...> {};
  // U is found, stop searching, get_index has member value with index I - 1
  template<typename U, unsigned I, typename ...T>
  struct get_index<U, I, true, T...> : std::enable_if_t<is_use_id_t<U>::value, 
    std::integral_constant<unsigned, I - 1>> {};
  // Get the index of a type that uses id among types that uses id in a list
  template<typename U, typename V, typename ...T>
  using get_id = get_index<U, std::conditional_t<is_use_id_t<V>::value, 
    std::integral_constant<unsigned, 1>,
    std::integral_constant<unsigned, 0>>::value, std::is_same_v<U, V>, T...>;

  /* Define types for index to __raptor_fp * mappings */
  using id_to_fp_t = std::vector<__raptor_fp *>; // Find __raptor_fp * with id
  using free_id_t = std::vector<size_t>; // Pool of free id that can be reused
  // Contains pointers to the id to __raptor_fp * mapping and pool of free ids
  struct id_map_info { id_to_fp_t * id_to_fp; free_id_t * free_id; };
  // Contains the index to __raptor_fp * mapping structures
  template<typename T, valid_use_id_t<T> = true> 
  struct id_map_t : id_map_info { 
    // id_to_fp are created with a dummy first element
    // because id == 0 is special case for __raptor_fprt_##FROM_TY##_check_zero
    id_to_fp_t id_to_fp{nullptr}; free_id_t free_id; 
    id_map_t() {
      id_map_info::id_to_fp = &id_to_fp;
      id_map_info::free_id = &free_id;
    }
  };
  // Create id_map and add to id_map_infos for each type that uses id in T...
  template<unsigned I, bool B, typename ...T>
  struct add_id_maps { 
    std::vector<id_map_info *> id_map_infos;
    using num_id_maps = std::integral_constant<size_t, I>;
    add_id_maps() { id_map_infos.resize(I, nullptr); }
    ~add_id_maps() { id_map_infos.clear(); } 
  };
  // Construct new id_map for type U that uses id
  template<unsigned I, typename U>
  struct add_id_maps<I, true, U> : add_id_maps<I+1, false> {
    using add_id_maps<I+1, false>::id_map_infos;
    add_id_maps<I, true, U>() { id_map_infos[I] = new id_map_t<U>(); }
    ~add_id_maps<I, true, U>() { delete id_map_infos[I]; }
  };
  // Construct new id_map for type U that uses id and continue with next type V
  template<unsigned I, typename U, typename V, typename ...T>
  struct add_id_maps<I, true, U, V, T...> : 
    add_id_maps<I + 1, is_use_id_t<V>::value, V, T...> {
    using add_id_maps<I + 1, is_use_id_t<V>::value, V, T...>::id_map_infos;
    add_id_maps<I, true, U, V, T...>() { id_map_infos[I] = new id_map_t<U>(); }
    ~add_id_maps<I, true, U, V, T...>() { delete id_map_infos[I]; }
  };
  // Do nothing for type U that does not use id and continue with next type V
  template<unsigned I, typename U, typename V, typename ...T>
  struct add_id_maps<I, false, U, V, T...> : 
    add_id_maps<I, is_use_id_t<V>::value, V, T...> {};
  // id_maps for types that uses id among U and T... with starting index I
  template<unsigned I, typename U, typename ...T>
  struct id_maps_t {};
  // id_maps for types that uses id among U and T...
  template<typename U, typename ...T>
  struct id_maps_t<0, U, T...> : add_id_maps<0, is_use_id_t<U>::value, U, T...>
  {
    using add_id_maps<0, is_use_id_t<U>::value, U, T...>::id_map_infos;
    using typename add_id_maps<0, is_use_id_t<U>::value, U, T...>::num_id_maps;
    template<typename V, valid_use_id_t<V> = true>
    const id_map_info& get_info() {
      static_assert(get_id<V, U, T...>::value < num_id_maps::value);
      return *id_map_infos[get_id<V, U, T...>::value];
    }
    void clear() {
      for (auto info : id_map_infos) {
        info->id_to_fp->clear(); info->id_to_fp->resize(1, nullptr);
        info->free_id->clear();
      }
    }
  };

  /* Functions to get index to __raptor_fp * mapping structures */
  // Get the index to __raptor_fp * mapping structure of type T that uses id
  template <typename T, valid_use_id_t<T> = true>
  inline id_map_info get_id_map_info();

  /* Functions to find __raptor_fp * given index and vice versa */
  // Convert f of type T that do not use id to __raptor_fp *
  template <typename T, valid_no_id_t<T> = true> 
  inline __raptor_fp * get_p_from_f(T f) {
    return raptor_bitcast<__raptor_fp *>(f);
  }
  // Get the __raptor_fp * corresponting to id f of type T that uses id 
  template <typename T, valid_use_id_t<T> = true> 
  inline __raptor_fp * get_p_from_f(T f) {
    return get_id_map_info<T>().id_to_fp->at(
      raptor_bitcast<fp_to_uint_t<T>>(f)); 
  }
  // Convert __raptor_fp * p to type T that do not use id
  template <typename T, valid_no_id_t<T> = true> 
  inline T get_f_from_p (__raptor_fp *p) { return raptor_bitcast<T>(p); }
  // Get the id converted to type T that uses size_t id from __raptor_fp * p
  template <typename T, use_id_t<T, size_t> = true>
  inline T get_f_from_p (__raptor_fp *p) { return raptor_bitcast<T>(p->id.d); }
  // Get the id converted to type T that uses 32-bit id from __raptor_fp * p
  template <typename T, use_id_t<T, uint32_t> = true>
  inline T get_f_from_p (__raptor_fp *p) { return raptor_bitcast<T>(p->id.f); }
  // Get the id converted to type T that uses 16-bit id from __raptor_fp * p
  template <typename T, use_id_t<T, uint16_t> = true>
  inline T get_f_from_p (__raptor_fp *p) { return raptor_bitcast<T>(p->id.h); }
  // Check and abort if id cannot be stored in type T
  template <typename T, valid_use_id_t<T> = true> 
  void check_id_overflow(__raptor_fp::ID &id) {
    using num_bits = std::integral_constant<unsigned, 
      sizeof(T) * std::numeric_limits<unsigned char>::digits>;
    if (id.d >= std::numeric_limits<fp_to_uint_t<T>>::max()) {
      std::cerr << "Exceeding maximum number (";
      std::cerr << std::numeric_limits<fp_to_uint_t<T>>::max() << ") of ";
      std::cerr << "truncated " << num_bits::value << "-bit floating point ";
      std::cerr << "values in mem-mode." << std::endl;
      abort();
    }
  }

  /* Functions to add/remove __raptor_fp * to/from the mapping structure */
  // Add __raptor_fp * to the mapping structure m and assign id
  template <typename T, valid_use_id_t<T> = true> 
  void add_fp(id_map_info & m, __raptor_fp * p) {
    m = get_id_map_info<T>(); // Set the pointers to the mapping structure
    if (m.free_id->empty()) { // Get new id if free pool is empty
      p->id.d = m.id_to_fp->size();
      check_id_overflow<T>(p->id); // Check new id can be stored in type T
      m.id_to_fp->push_back(p); // Add mapping of new id to p
    } else { // Use available free id if the pool is not empty
      p->id.d = m.free_id->back(); // Get a free id
      m.free_id->pop_back(); // Remove it from the free pool
      m.id_to_fp->at(p->id.d) = p; // Add mapping of free id to p
    }
  }
  // Remove __raptor_fp * mapped to id from the index mapping structure m
  void remove_fp(id_map_info & m, __raptor_fp::ID &id) {
    if (m.id_to_fp) { // If mapping structures are used
      m.free_id->push_back(id.d); // Add id to pool of free ids
      m.id_to_fp->at(id.d) = nullptr; // Unregister fp from id_to_fp mapping
    }
  }
};

struct GCFloatTy {
  __raptor_fp fp;
  bool seen;
  // Record where the fp is added to remove it when destructing
  gcfloatidmap::id_map_info id_map = {nullptr, nullptr};
  GCFloatTy() : seen(false) {}
  ~GCFloatTy() { gcfloatidmap::remove_fp(id_map, fp.id); }
  // Set id_map that the fp is added to and assign fp.id if T uses id
  template <typename T, gcfloatidmap::valid_use_id_t<T> = true>
  void set_raptor_fp_id() { gcfloatidmap::add_fp<T>(id_map, &fp); }
  // Do nothing if T does not use id
  template <typename T, gcfloatidmap::valid_no_id_t<T> = true>
  void set_raptor_fp_id() {}
};
struct {
  #define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY) , CPP_TY
  gcfloatidmap::id_maps_t<0
    #include "raptor/FloatTypes.def"
  > id_maps;
  std::list<GCFloatTy> all;
  void clear() { all.clear(); id_maps.clear(); }

} __raptor_mpfr_fps;

template <typename T, gcfloatidmap::valid_use_id_t<T>>
inline gcfloatidmap::id_map_info gcfloatidmap::get_id_map_info() {
  return __raptor_mpfr_fps.id_maps.get_info<T>();
}


#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  __raptor_fp * get_raptor_fp_from_##FROM_TY(CPP_TY d) {                       \
    return gcfloatidmap::get_p_from_f(d);                                      \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY get_##FROM_TY##_from_raptor_fp(__raptor_fp *p) {                      \
    return gcfloatidmap::get_f_from_p<CPP_TY>(p);                              \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_get(CPP_TY _a, int64_t exponent,            \
                                       int64_t significand, int64_t mode,      \
                                       const char *loc, void *scratch) {       \
    __raptor_fp *a = __raptor_fprt_##FROM_TY##_to_ptr(_a);                     \
    return mpfr_get_d(a->result, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);         \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_new(CPP_TY _a, int64_t exponent,            \
                                       int64_t significand, int64_t mode,      \
                                       const char *loc, void *scratch) {       \
    __raptor_mpfr_fps.all.push_back({});                                       \
    __raptor_mpfr_fps.all.back().set_raptor_fp_id<CPP_TY>();                   \
    __raptor_fp *a = &__raptor_mpfr_fps.all.back().fp;                         \
    mpfr_init2(a->result, significand + 1); /* see MPFR_FP_EMULATION */        \
    mpfr_set_d(a->result, _a, __RAPTOR_MPFR_DEFAULT_ROUNDING_MODE);            \
    a->excl_result = _a;                                                       \
    a->shadow = _a;                                                            \
    return __raptor_fprt_ptr_to_##FROM_TY(a);                                  \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  CPP_TY __raptor_fprt_##FROM_TY##_const(CPP_TY _a, int64_t exponent,          \
                                         int64_t significand, int64_t mode,    \
                                         const char *loc, void *scratch) {     \
    /* TODO This should really be called only once for an appearance in the    \
     * code, currently it is called every time a flop uses a constant. */      \
    return __raptor_fprt_##FROM_TY##_new(_a, exponent, significand, mode, loc, \
                                         scratch);                             \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  __raptor_fp *__raptor_fprt_##FROM_TY##_new_intermediate(                     \
      int64_t exponent, int64_t significand, int64_t mode, const char *loc,    \
      void *scratch) {                                                         \
    __raptor_mpfr_fps.all.push_back({});                                       \
    __raptor_mpfr_fps.all.back().set_raptor_fp_id<CPP_TY>();                   \
    __raptor_fp *a = &__raptor_mpfr_fps.all.back().fp;                         \
    mpfr_init2(a->result, significand + 1); /* see MPFR_FP_EMULATION */        \
    return a;                                                                  \
  }                                                                            \
                                                                               \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_fprt_##FROM_TY##_delete(CPP_TY a, int64_t exponent,            \
                                        int64_t significand, int64_t mode,     \
                                        const char *loc, void *scratch) {      \
    /* ignore for now */                                                       \
  }
#include "raptor/FloatTypes.def"

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_dump_status() {
  std::cerr << "Currently " << __raptor_mpfr_fps.all.size()
            << " floats allocated." << std::endl;
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_clear_seen() {
  for (auto &gcfp : __raptor_mpfr_fps.all)
    gcfp.seen = false;
}

__RAPTOR_MPFR_ATTRIBUTES
double raptor_fprt_gc_mark_seen(double a) {
  __raptor_fp *fp = __raptor_fprt_ieee_64_to_ptr(a);
  if (!fp)
    return a;
  intptr_t offset = (char *)&(((GCFloatTy *)nullptr)->fp) - (char *)nullptr;
  GCFloatTy *gcfp = (GCFloatTy *)((char *)fp - offset);
  gcfp->seen = true;
  return a;
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_gc_doit() {
  for (auto it = __raptor_mpfr_fps.all.begin();
       it != __raptor_mpfr_fps.all.end();) {
    if (!it->seen) {
      mpfr_clear(it->fp.result);
      it = __raptor_mpfr_fps.all.erase(it);
    } else {
      ++it;
    }
  }
  raptor_fprt_gc_clear_seen();
  // TODO: resize id mapping structures if many are erased?
}

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_start() { excl_trunc = true; }

__RAPTOR_MPFR_ATTRIBUTES
void raptor_fprt_excl_trunc_end() { excl_trunc = false; }
