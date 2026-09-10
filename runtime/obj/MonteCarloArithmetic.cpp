#include <iostream>
#include <type_traits>

// verificarlo uses _Float128 that needs extra definition
#if defined(__RAPTOR_VERIFICARLOMCA_QUAD_MODE) ||                              \
    defined(__RAPTOR_VERIFICARLOMCA_INT_MODE)
  // Clang has "unknown type name" error for _Float128 on x86
  #if defined(__clang__) && (defined(__i386) || defined(__x86_64))
    #define _Float128 __float128
  #endif
  // Need to be defined before including mpfr.h to enable binary128 support
  #define MPFR_WANT_FLOAT128
#endif
// mpfr.h is included in Common.h
#include <raptor/Common.h>

#if defined(__RAPTOR_VERIFICARLOMCA_QUAD_MODE) ||                              \
    defined(__RAPTOR_VERIFICARLOMCA_INT_MODE)
  #define __RAPTOR_USE_VERIFICARLOMCA true
  // Includes needed to define interface to verificarlo
  #include <argp.h>
  #include <err.h>
  #include <sys/syscall.h>
  #include <sys/time.h>
  #include <unistd.h>
  #if defined(__cplusplus)
  extern "C" {
  #endif
  #ifdef __RAPTOR_VERIFICARLOMCA_INT_MODE
    #define __RAPTOR_VERIFICARLOMCA_HAS_INT true
    #include <interflop/interflop_mca_int.h>
    extern void _mcaint_inexact_binary64(double *da, void *context);
    extern void _mcaint_inexact_binary128(_Float128 *qa, void *context);
    #define __RAPTOR_VERIFICARLOMCA_INT_INTERFLOP_CALL(name, ...) \
      do { INTERFLOP_MCAINT_API(name)(__VA_ARGS__); } while (0)
    #define __RAPTOR_VERIFICARLOMCA_INT_INEXACT_CALL(bits, ...) \
      do { _mcaint_inexact_binary##bits(__VA_ARGS__); } while (0)
  #else
    #define __RAPTOR_VERIFICARLOMCA_HAS_INT false
    #define __RAPTOR_VERIFICARLOMCA_INT_INTERFLOP_CALL(name, ...)
    #define __RAPTOR_VERIFICARLOMCA_INT_INEXACT_CALL(bits, ...)
  #endif
  #ifdef __RAPTOR_VERIFICARLOMCA_QUAD_MODE
    #define __RAPTOR_VERIFICARLOMCA_HAS_QUAD true
    #include <interflop/interflop_mca.h>
    extern void _mcaquad_inexact_binary64(double *da, void *context);
    extern void _mcaquad_inexact_binary128(_Float128 *qa, void *context);
    #define __RAPTOR_VERIFICARLOMCA_INTERFLOP_CALL(name, ...) \
      do { INTERFLOP_MCAQUAD_API(name)(__VA_ARGS__); } while (0)
    #define __RAPTOR_VERIFICARLOMCA_INEXACT_CALL(bits, ...) \
      do { _mcaquad_inexact_binary##bits(__VA_ARGS__); } while (0)
  #else 
    #define __RAPTOR_VERIFICARLOMCA_HAS_QUAD false
    #define __RAPTOR_VERIFICARLOMCA_INTERFLOP_CALL(name, ...)
    #define __RAPTOR_VERIFICARLOMCA_INEXACT_CALL(bits, ...)
  #endif
  #if defined(__cplusplus)
  }
  #endif
  #if __RAPTOR_VERIFICARLOMCA_HAS_INT && __RAPTOR_VERIFICARLOMCA_HAS_QUAD
    #define __RAPTOR_VERIFICARLO_MCA_TYPE(name)                                \
      std::conditional_t<IS_MCAINT, mcaint_##name, mcaquad_##name>
    #define __RAPTOR_VERIFICARLO_MCA_ENUM(type, name)                          \
      mca_##type<IS_MCAINT>(IS_MCAINT? mcaint_##type::mcaint_##name :          \
                                       mcaquad_##type::mcaquad_##name)
  #elif __RAPTOR_VERIFICARLOMCA_HAS_INT
    #define __RAPTOR_VERIFICARLO_MCA_TYPE(name) mcaint_##name
    #define __RAPTOR_VERIFICARLO_MCA_ENUM(type, name)                          \
      mcaint_##type::mcaint_##name
  #else // __RAPTOR_VERIFICARLOMCA_HAS_QUAD
    #define __RAPTOR_VERIFICARLO_MCA_TYPE(name) mcaquad_##name
    #define __RAPTOR_VERIFICARLO_MCA_ENUM(type, name)                          \
      mcaquad_##type::mcaquad_##name
  #endif
  
  // Enclose types/functions to interface with verificarlo in unnamed namespace
  namespace {
    // Using a struct because the context needs initialization
    struct verificarlo_mca_context_t {
      // Helper type for getting the correct mca context type
      template<bool IS_MCAINT>
      using mca_context_t = __RAPTOR_VERIFICARLO_MCA_TYPE(context_t);
      // Helper type for getting the correct mca configure type
      template<bool IS_MCAINT>
      using mca_conf_t = __RAPTOR_VERIFICARLO_MCA_TYPE(conf_t);
      // Helper type for getting the correct mca mode type
      template<bool IS_MCAINT>
      using mca_mode = __RAPTOR_VERIFICARLO_MCA_TYPE(mode);
      // Helper type for getting the correct mca error mode type
      template<bool IS_MCAINT>
      using mca_err_mode = __RAPTOR_VERIFICARLO_MCA_TYPE(err_mode);

      // Helper type for compile time type check
      template<typename T>
      using is_float_t = std::enable_if_t<std::is_same_v<T, float>, bool>;
      template<typename T>
      using is_double_t = std::enable_if_t<std::is_same_v<T, double>, bool>;
      template<typename T>
      using is__Float128_t = std::enable_if_t<std::is_same_v<T, _Float128>, 
                                              bool>;
      template<typename T>
      using is_float_or_double_t = std::enable_if_t<
        std::is_same_v<T, float> || std::is_same_v<T, double>, bool>;
      template<typename T>
      using is_double_or__Float128_t = std::enable_if_t<
        std::is_same_v<T, double> || std::is_same_v<T, _Float128>, bool>;
      template<typename T, is_float_or_double_t<T> = true>
      using inexact_t = std::conditional_t<std::is_same_v<T, float>, double, 
                                                                    _Float128>;
      
      // Helper variable template to get the correct mca mode enum value
      template<bool IS_MCAINT>
      static constexpr mca_mode<IS_MCAINT> mca_mode_ieee = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(mode, mode_ieee);
      template<bool IS_MCAINT>
      static constexpr mca_mode<IS_MCAINT> mca_mode_rr = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(mode, mode_rr);
      template<bool IS_MCAINT>
      static constexpr mca_mode<IS_MCAINT> mca_mode_pb = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(mode, mode_pb);
      template<bool IS_MCAINT>
      static constexpr mca_mode<IS_MCAINT> mca_mode_mca = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(mode, mode_mca);
      
      // Helper variable template to get the correct mca error mode enum value
      template<bool IS_MCAINT>
      static constexpr mca_err_mode<IS_MCAINT> mca_err_mode_rel = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(err_mode, err_mode_rel);
      template<bool IS_MCAINT>
      static constexpr mca_err_mode<IS_MCAINT> mca_err_mode_abs = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(err_mode, err_mode_abs);
      template<bool IS_MCAINT>
      static constexpr mca_err_mode<IS_MCAINT> mca_err_mode_all = 
        __RAPTOR_VERIFICARLO_MCA_ENUM(err_mode, err_mode_all);

      // Helper variable template to get mca_conf_t with default values
      template<bool IS_MCAINT>
      static constexpr mca_conf_t<IS_MCAINT> default_mca_conf = {
        .seed = 0ULL, // Default to 0, but always sets the seed to chosen
        // If random (0,1) num > sparsity, MCA is not applied.
        .sparsity = 1.0f, // Always apply MCA
        // Between 1 and double precision pseudo-mantissa encoding size (52)
        .precision_binary32 = 24, // default to float mantissa size (23+1)
        // Between 1 and quad precision pseudo mantissa encoding size (112)
        .precision_binary64 = 53, // default to double mantissa size (52+1)
        // Only add inexact to input operands.
        .mode = mca_mode_pb<IS_MCAINT>, // default to only perturb inbound
        // The mode matching formula (1) in Verificarlo
        .err_mode = mca_err_mode_rel<IS_MCAINT>,
        // Unused for relative error mode mca_err_mode_rel
        .max_abs_err_exponent = 112, // default from verificarlo set to 112
        // Default to false, not dealing with this now 
        .daz = 0, // 0 for false, 1 for true
        .ftz = 0 // 0 for false, 1 for true
      };

      // Function and definition adapted from verificarlo repo file 
      // src/vfcwrapper/main.c.in
      static constexpr int MAX_ARGS=256;
      static void get_args_from_str(char *str, const char *err_msg_prefix, 
                                    int &argc, char *argv[MAX_ARGS]) 
      {
        if (str != NULL) {
          char *spaceptr;
          char *arg = strtok_r(str, " ", &spaceptr);
          while (arg) {
            if (argc >= MAX_ARGS) {
              fprintf(stderr, "%s syntax error: too many arguments", 
                      err_msg_prefix);
            }
            argv[argc++] = arg;
            arg = strtok_r(NULL, " ", &spaceptr);
          }
          argv[argc] = NULL;
        }
      }
      // Return the high precision type used for MCA calculation from mpfr_t a,
      // with the rounding mode rnd_mode
      template<typename T, is_float_t<T> = true>
      static inexact_t<T> get_inexact_t_from(mpfr_t a, mpfr_rnd_t rnd_mode) {
        return mpfr_get_d(a, rnd_mode);
      }
      template<typename T, is_double_t<T> = true>
      static inexact_t<T> get_inexact_t_from(mpfr_t a, mpfr_rnd_t rnd_mode) {
        return mpfr_get_float128(a, rnd_mode);
      }
      // Assigns mpfr_t a with the value of val with rnd_mode rounding mode,
      // where val is of the high precision type used for MCA calculation.
      // Returns the return value from mpfr_set_* used underneath.
      template<typename T, is_float_t<T> = true>
      static int assign_inexact_t_to(mpfr_t a, inexact_t<T> val, 
                                     mpfr_rnd_t rnd_mode) 
      {
        return mpfr_set_d(a, val, rnd_mode);
      }
      template<typename T, is_double_t<T> = true>
      static int assign_inexact_t_to(mpfr_t a, inexact_t<T> val, 
                                     mpfr_rnd_t rnd_mode) 
      {
        return mpfr_set_float128(a, val, rnd_mode);
      }

      // Context used for the _mca*_inexact_binary64 functions
      void * context = nullptr;
      // Flag indicating whether context is mcaint_context_t or not
      bool is_mcaint = __RAPTOR_VERIFICARLOMCA_HAS_INT;

      // Set the context with configure
      void set_verificarlo_mca_context(void *configure) {
        if (is_mcaint) { 
          __RAPTOR_VERIFICARLOMCA_INT_INTERFLOP_CALL(configure, 
                                                    configure, context);
        } else {
          __RAPTOR_VERIFICARLOMCA_INTERFLOP_CALL(configure, configure, context);
        }
      }
      void set_verificarlo_mca_context(int argc, char **argv) {
        if (is_mcaint) { 
          __RAPTOR_VERIFICARLOMCA_INT_INTERFLOP_CALL(cli, argc, argv, context);
        } else {
          __RAPTOR_VERIFICARLOMCA_INTERFLOP_CALL(cli, argc, argv, context);
        }
      }
      // Returns the virtual precision used for MCA of floating point type T
      template<typename T, bool IS_MCAINT, is_float_t<T> = true>
      IUint32_t _get_virtual_prec() {
        return ((mca_context_t<IS_MCAINT> *)context)->binary32_precision;
      }
      template<typename T, bool IS_MCAINT, is_double_t<T> = true>
      IUint32_t _get_virtual_prec() {
        return ((mca_context_t<IS_MCAINT> *)context)->binary64_precision;
      }
      template<typename T, is_float_or_double_t<T> = true>
      IUint32_t get_virtual_prec() {
        if (is_mcaint) { return _get_virtual_prec<T, true>(); } 
        else { return _get_virtual_prec<T, false>(); }
      }
      // Returns the MCA mode (ieee, rr, pb, or mca)
      template<bool IS_MCAINT>
      mca_mode<IS_MCAINT> get_mode() {
        return ((mca_context_t<IS_MCAINT> *)context)->mode;
      }
      // Returns if MCA random perturbation should be added based on mode
      template<bool IS_MCAINT>
      bool should_inexact(bool isOutbound) {
        auto mode = get_mode<IS_MCAINT>();
        return isOutbound ?
          (mode == mca_mode_rr<IS_MCAINT> || mode == mca_mode_mca<IS_MCAINT>) :
          (mode == mca_mode_pb<IS_MCAINT> || mode == mca_mode_mca<IS_MCAINT>);
      }
      // Set the virtual precision used for MCA of floating point type T
      template<typename T, is_float_or_double_t<T> = true>
      void set_virtual_prec(IUint32_t virtual_prec) {
        int argc;
        char *argv[MAX_ARGS];
        std::string str = is_mcaint? "libinterflop_mca_int.so" : 
                                        "libinterflop_mca.so";
        constexpr std::string_view nbits = std::is_same_v<T, float>? 
          " --precision-binary32" : " --precision-binary64";
        str += nbits;
        str += "=" + std::to_string(virtual_prec);
        get_args_from_str(str.data(), ("set_virtual_prec " + str).c_str(), 
                          argc, argv);
        set_verificarlo_mca_context(argc, argv);
      }
      // Apply MCA random perturbation to a of type T, with the parameters for 
      // MCA defined in context
      template<typename T, is_double_t<T> = true>
      void mca_inexact(T &a) {
        if (is_mcaint) { 
          __RAPTOR_VERIFICARLOMCA_INT_INEXACT_CALL(64, &a, context);
        } else { 
          __RAPTOR_VERIFICARLOMCA_INEXACT_CALL(64, &a, context);
        }
      }
      template<typename T, is__Float128_t<T> = true>
      void mca_inexact(T &a) {
        if (is_mcaint) { 
          __RAPTOR_VERIFICARLOMCA_INT_INEXACT_CALL(128, &a, context);
        } else { 
          __RAPTOR_VERIFICARLOMCA_INEXACT_CALL(128, &a, context);
        }
      }
      
      verificarlo_mca_context_t();
    };

    #if defined(__cplusplus)
    extern "C" {
    #endif
    // Function definitions copied from verificarlo repo file
    // src/vfcwrapper/main.c.in
    void _vfc_panic(const char *msg) { fprintf(stderr, "%s", msg); exit(1); }
    pid_t get_tid() { return syscall(__NR_gettid); }
    long _vfc_strtol(const char *nptr, char **endptr, int *error) {
      *error = 0;
      errno = 0;
      long val = strtoll(nptr, endptr, 10);
      if (errno != 0) {
        *error = 1;
      }
      return val;
    }
    double _vfc_strtod(const char *nptr, char **endptr, int *error) {
      *error = 0;
      errno = 0;
      double val = strtod(nptr, endptr);
      if (errno != 0) {
        *error = 1;
      }
      return val;
    }
    // Function adapted from verificarlo repo file 
    // src/vfcwrapper/main.c.in
    /* Parse the different VFC_BACKENDS variables per priorty order */
    /* 1- VFC_BACKENDS */
    /* 2- VFC_BACKENDS_FROM_FILE */
    /* Set the backends read in vfc_backends */
    /* Set the name of the environment variable read in vfc_backends_env */
    void parse_vfc_backends_env(int &backend_argc, 
      char *backend_argv[verificarlo_mca_context_t::MAX_ARGS]) 
    {
      char *vfc_backends_v = NULL;
      const char *vfc_backends_env_v = NULL;
      char ** vfc_backends = &vfc_backends_v;
      const char ** vfc_backends_env = &vfc_backends_env_v;

      /* Parse VFC_BACKENDS */
      *vfc_backends_env = "VFC_BACKENDS";
      char *env_val = getenv(*vfc_backends_env);
      if (env_val != NULL) {
        size_t env_len = strlen(env_val);
        *vfc_backends = (char *)malloc(env_len + 1);
        if (*vfc_backends == NULL) {
          fprintf(stderr, "Memory allocation failed for %s", 
                          *vfc_backends_env);
        }
        strcpy(*vfc_backends, env_val);
      } else {
        *vfc_backends = NULL;
      }

      /* Parse VFC_BACKENDS_FROM_FILE if VFC_BACKENDS is empty*/
      if (*vfc_backends == NULL) {
        *vfc_backends_env = "VFC_BACKENDS_FROM_FILE";
        char *vfc_backends_fromfile_file = getenv(*vfc_backends_env);
        if (vfc_backends_fromfile_file != NULL) {
          FILE *fi = fopen(vfc_backends_fromfile_file, "r");
          if (fi == NULL) {
            fprintf(stderr, "Error while opening file pointed by %s: %s",
                        *vfc_backends_env, strerror(errno));
          } else {
            size_t len = 0;
            ssize_t nread;
            nread = getline(vfc_backends, &len, fi);
            if (nread == -1) {
              fprintf(stderr, "Error while reading file pointed by %s: %s",
                          *vfc_backends_env, strerror(errno));
            } else {
              if ((*vfc_backends)[nread - 1] == '\n') {
                (*vfc_backends)[nread - 1] = '\0';
              }
            }
          }
        }
      }
      
      verificarlo_mca_context_t::get_args_from_str(
        vfc_backends_v, vfc_backends_env_v, backend_argc, backend_argv);
    }
    #if defined(__cplusplus)
    }
    #endif

    // Interflop needs some set up, then allocate and set context
    verificarlo_mca_context_t::verificarlo_mca_context_t() {
      interflop_set_handler("argp_parse", (void *)argp_parse);
      interflop_set_handler("panic", (void *)_vfc_panic);
      interflop_set_handler("exit", (void *)exit);
      interflop_set_handler("fopen", (void *)fopen);
      interflop_set_handler("fprintf", (void *)fprintf);
      interflop_set_handler("getenv", (void *)getenv);
      interflop_set_handler("gettid", (void *)get_tid);
      interflop_set_handler("malloc", (void *)malloc);
      interflop_set_handler("sprintf", (void *)sprintf);
      interflop_set_handler("strcasecmp", (void *)strcasecmp);
      interflop_set_handler("strerror", (void *)strerror);
      interflop_set_handler("strtol", (void *)_vfc_strtol);
      interflop_set_handler("strtod", (void *)_vfc_strtod);
      interflop_set_handler("vfprintf", (void *)vfprintf);
      interflop_set_handler("vwarnx", (void *)vwarnx);
      interflop_set_handler("gettimeofday", (void *)gettimeofday);
      // Skipping interflop init since it is mostly registering hooked function
      // for instrumentation (and we are doing it in RAPTOR instead)
      int backend_argc = 0;
      char *backend_argv[MAX_ARGS];
      parse_vfc_backends_env(backend_argc, backend_argv);
      // Setup which library is used and assign is_mcaint
      if (backend_argc > 0) {
        std::string libname = backend_argv[0];
        if (libname == "libinterflop_mca_int.so") {
          is_mcaint = __RAPTOR_VERIFICARLOMCA_HAS_INT;
          if (!is_mcaint) {
            std::cerr << "Error: " << libname << " selected through ";
            std::cerr << "VFC_BACKENDS but not included in RAPTOR build. ";
            std::cerr << std::endl; abort();
          }
        } else if (libname == "libinterflop_mca.so") {
          is_mcaint = !__RAPTOR_VERIFICARLOMCA_HAS_QUAD;
          if (is_mcaint) {
            std::cerr << "Error: " << libname << " selected through ";
            std::cerr << "VFC_BACKENDS but not included in RAPTOR build. ";
            std::cerr << std::endl; abort();
          }
        } else {
          std::cerr << "Error: " << libname << " is not a valid backend.";
          std::cerr << std::endl; abort();
        }
      }
      // pre_init also allocates and initialize the context
      if (is_mcaint) { 
        __RAPTOR_VERIFICARLOMCA_INT_INTERFLOP_CALL(pre_init, 
                                                   _vfc_panic, stderr, 
                                                   &context);
      } else { 
        __RAPTOR_VERIFICARLOMCA_INTERFLOP_CALL(pre_init, 
                                               _vfc_panic, stderr, &context);
      }
      if (backend_argc > 0) {
        set_verificarlo_mca_context(backend_argc, backend_argv);
      } else {
        // Default configuration used to initialize the context
        // Also used to set the context when configuration changes
        if (is_mcaint) {
          mca_conf_t<true> mca_conf = default_mca_conf<true>;
          set_verificarlo_mca_context(&mca_conf);
        } else {
          mca_conf_t<false> mca_conf = default_mca_conf<false>;
          set_verificarlo_mca_context(&mca_conf);
        }
      }
    }
    // Global so that it gets automatically initialized through construction
    verificarlo_mca_context_t verificarlo_mca_context;
  } // end of unnamed namespace

  // verificarlo does not change virtual precision mid run, just get from conf
  #define __RAPTOR_VERIFICARLOMCA_get_virtual_prec(CPP_TY)                     \
    do {                                                                       \
      return verificarlo_mca_context.get_virtual_prec<CPP_TY>();               \
    } while (0)
  
  #define __RAPTOR_VERIFICARLOMCA_inexact(CPP_TY, a, virtual_prec, rnd_mode,   \
    isOutbound)                                                                \
    do {                                                                       \
      bool addInexact = verificarlo_mca_context.is_mcaint?                     \
        verificarlo_mca_context.should_inexact<true>(isOutbound) :             \
        verificarlo_mca_context.should_inexact<false>(isOutbound);             \
      if (addInexact) {                                                        \
        if (virtual_prec !=                                                    \
            verificarlo_mca_context.get_virtual_prec<CPP_TY>()                 \
        ) {                                                                    \
          verificarlo_mca_context.set_virtual_prec<CPP_TY>(virtual_prec);      \
        }                                                                      \
        verificarlo_mca_context_t::inexact_t<CPP_TY> high_prec_a =             \
          verificarlo_mca_context_t::get_inexact_t_from<CPP_TY>(a, rnd_mode);  \
        verificarlo_mca_context.mca_inexact(high_prec_a);                      \
        verificarlo_mca_context_t::assign_inexact_t_to<CPP_TY>(a, high_prec_a, \
                                                               rnd_mode);      \
      }                                                                        \
    } while (0) 
#else
  #define __RAPTOR_USE_VERIFICARLOMCA false
  #define __RAPTOR_VERIFICARLOMCA_get_virtual_prec(CPP_TY)
  #define __RAPTOR_VERIFICARLOMCA_inexact(CPP_TY, a, virtual_prec, rnd_mode,   \
                                          isOutbound)
#endif // defined(__RAPTOR_VERIFICARLOMCA_QUAD_MODE) ||
       // defined(__RAPTOR_VERIFICARLOMCA_INT_MODE)

#define RAPTOR_FLOAT_TYPE(CPP_TY, FROM_TY)                                     \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  unsigned int __raptor_mca_get_virtural_prec_##FROM_TY(mpfr_t a,              \
                                                        const char *loc) {     \
    if constexpr (__RAPTOR_USE_VERIFICARLOMCA) {                               \
      __RAPTOR_VERIFICARLOMCA_get_virtual_prec(CPP_TY);                        \
    } else {                                                                   \
      std::cerr << "__raptor_mca_get_virtural_prec_" << #FROM_TY;              \
      std::cerr << " is not implemented." << std::endl;                        \
      abort();                                                                 \
    }                                                                          \
  }                                                                            \
  __RAPTOR_MPFR_ATTRIBUTES                                                     \
  void __raptor_mca_inexact_##FROM_TY(mpfr_t a, unsigned int virtual_prec,     \
                                      mpfr_rnd_t rnd_mode, bool isOutbound) {  \
    if constexpr (__RAPTOR_USE_VERIFICARLOMCA) {                               \
      __RAPTOR_VERIFICARLOMCA_inexact(CPP_TY, a, virtual_prec, rnd_mode,       \
        isOutbound);                                                           \
    } else {                                                                   \
      std::cerr << "__raptor_mca_inexact_" << #FROM_TY;                        \
      std::cerr << " is not implemented." << std::endl;                        \
      abort();                                                                 \
    }                                                                          \
  }
#include "raptor/FloatTypes.def"