cmake_minimum_required(VERSION 3.13)
project(Raptor)

include(CMakePackageConfigHelpers)
include(CheckIncludeFile)
include(CheckIncludeFileCXX)

set(RAPTOR_MAJOR_VERSION 0)
set(RAPTOR_MINOR_VERSION 0)
set(RAPTOR_PATCH_VERSION 0)
set(RAPTOR_VERSION ${RAPTOR_MAJOR_VERSION}.${RAPTOR_MINOR_VERSION}.${RAPTOR_PATCH_VERSION})

add_definitions(-DRAPTOR_VERSION_MAJOR=${RAPTOR_MAJOR_VERSION})
add_definitions(-DRAPTOR_VERSION_MINOR=${RAPTOR_MINOR_VERSION})
add_definitions(-DRAPTOR_VERSION_PATCH=${RAPTOR_PATCH_VERSION})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
SET(CMAKE_CXX_FLAGS "-Wall -fno-rtti ${CMAKE_CXX_FLAGS} -Werror=dangling-else -Werror=unused-but-set-variable -Werror=return-type -Werror=nonnull -Werror=unused-result -Werror=reorder -Werror=switch")
SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "-O2")

SET(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -ggdb -fno-omit-frame-pointer")

#SET(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -fno-omit-frame-pointer -fsanitize=address")
#SET(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RAPTOR_CLANG "Build raptor clang plugin" ON)
option(RAPTOR_FLANG "Build raptor flang symlink" OFF)
option(RAPTOR_MLIR "Build raptor mlir plugin" OFF)
option(RAPTOR_IFX "Enable raptor support for the Intel Fortran compiler IFX" OFF)
option(RAPTOR_EXTERNAL_SHARED_LIB "Build external shared library" OFF)
option(RAPTOR_STATIC_LIB "Build static library" OFF)
set(RAPTOR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(RAPTOR_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH "${RAPTOR_SOURCE_DIR}/cmake/modules")

set(LLVM_SHLIBEXT "${CMAKE_SHARED_MODULE_SUFFIX}")
if (APPLE)
    set(LLVM_SHLIBEXT ".dylib")
endif()
message( LLVM_SHLIBEXT = ${LLVM_SHLIBEXT} )

set(LLVM_MAIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

get_filename_component(LLVM_ABSOLUTE_DIR
    "${LLVM_DIR}"
    REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

if (NOT EXISTS "${LLVM_DIR}")
  message("Looking for LLVM_DIR at ${LLVM_DIR}")
  message(SEND_ERROR "The given LLVM_DIR does not exist. Typo?")
endif()

set(LLVM_DIR "${LLVM_ABSOLUTE_DIR}" CACHE FILEPATH "b" FORCE)

if (EXISTS "${LLVM_DIR}/lib/cmake/llvm/LLVMConfig.cmake")
  set(LLVM_DIR "${LLVM_DIR}/lib/cmake/llvm")
endif()

message("LLVM ABS DIR " ${LLVM_DIR})

message("CMAKE_PREFIX_PATH " ${CMAKE_PREFIX_PATH})

find_package(LLVM REQUIRED CONFIG)

if (NOT DEFINED LLVM_EXTERNAL_LIT)
  if(LLVM_DIR MATCHES ".*/cmake/llvm/?$")
      message("found llvm match ${CMAKE_MATCH_1} dir ${LLVM_DIR}")
    if (EXISTS ${LLVM_DIR}/../../../bin/llvm-lit) 
      set(LLVM_EXTERNAL_LIT ${LLVM_DIR}/../../../bin/llvm-lit) 
    else()
      set(LLVM_EXTERNAL_LIT lit)
    endif()
  else()
    if (EXISTS ${LLVM_DIR}/bin/llvm-lit) 
      set(LLVM_EXTERNAL_LIT ${LLVM_DIR}/bin/llvm-lit) 
    else()
      set(LLVM_EXTERNAL_LIT lit)
    endif()
  endif()
endif()

get_filename_component(LLVM_ABSOLUTE_LIT
                       "${LLVM_EXTERNAL_LIT}"
                       REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

                   set(LLVM_EXTERNAL_LIT "${LLVM_ABSOLUTE_LIT}" CACHE FILEPATH "a" FORCE)
message("found llvm lit " ${LLVM_EXTERNAL_LIT})

list(INSERT CMAKE_PREFIX_PATH 0 "${LLVM_DIR}")
message("LLVM dir ${LLVM_DIR}")
if (RAPTOR_CLANG)
if (DEFINED Clang_DIR)
    get_filename_component(Clang_ABSOLUTE_DIR
      "${Clang_DIR}"
    REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
set(Clang_DIR "${Clang_ABSOLUTE_DIR}" CACHE FILEPATH "b" FORCE)
  list(INSERT CMAKE_PREFIX_PATH 0 "${Clang_DIR}")
  message("clang dir defined ${Clang_DIR}")
else()
  if(LLVM_DIR MATCHES ".*/cmake/llvm/?$")
    if (EXISTS ${LLVM_DIR}/../clang/../../libclangBasic.a)
      set(Clang_DIR ${LLVM_DIR}/../clang)
     list(INSERT CMAKE_PREFIX_PATH 0 "${Clang_DIR}")
    endif()
  elseif(LLVM_DIR MATCHES ".*/llvm-([0-9]+)/?$")
    if (EXISTS ${LLVM_DIR}/lib/libclangBasic.a)
      set(Clang_DIR ${LLVM_DIR}/lib/cmake/clang)
     list(INSERT CMAKE_PREFIX_PATH 0 "${Clang_DIR}")
    endif()
  elseif(LLVM_DIR MATCHES ".*/llvm-([0-9]+)/cmake/?$")
      get_filename_component(LLDIR "${LLVM_DIR}" DIRECTORY)
    if (EXISTS ${LLDIR}/lib/libclangBasic.a)
      set(Clang_DIR ${LLDIR}/lib/cmake/clang)
     list(INSERT CMAKE_PREFIX_PATH 0 "${Clang_DIR}")
    endif()
  else()
    if (EXISTS ${LLVM_DIR}/lib/libclangBasic.a)
      set(Clang_DIR ${LLVM_DIR})
      list(INSERT CMAKE_PREFIX_PATH 0 "${Clang_DIR}")
    endif()
  endif()
  message("clang dir from llvm ${Clang_DIR}")
endif()
else()
endif()

if (RAPTOR_MLIR)
    if (DEFINED MLIR_DIR)
        get_filename_component(MLIR_ABSOLUTE_DIR
        "${MLIR_DIR}"
        REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
        set(MLIR_DIR "${MLIR_ABSOLUTE_DIR}" CACHE FILEPATH "b" FORCE)
        list(INSERT CMAKE_PREFIX_PATH 0 "${MLIR_DIR}")
        message("MLIR dir defined ${MLIR_DIR}")
    else()
      if(LLVM_DIR MATCHES ".*/cmake/llvm/?$")
        if (EXISTS ${LLVM_DIR}/../mlir/../../libMLIRIR.a)
            set(MLIR_DIR ${LLVM_DIR}/../mlir)
            list(INSERT CMAKE_PREFIX_PATH 0 "${MLIR_DIR}")
        else()
            message(SEND_ERROR "MLIR requested but not found")
        endif()
      elseif(LLVM_DIR MATCHES ".*/llvm-([0-9]+)/?$")
        if (EXISTS ${LLVM_DIR}/lib/libMLIRIR.a)
            set(MLIR_DIR ${LLVM_DIR}/lib/cmake/mlir)
          list(INSERT CMAKE_PREFIX_PATH 0 "${MLIR_DIR}")
        else()
            message(SEND_ERROR "MLIR requested but not found")
        endif()
      elseif(LLVM_DIR MATCHES ".*/llvm-([0-9]+)/cmake/?$")
          get_filename_component(LLDIR "${LLVM_DIR}" DIRECTORY)
        if (EXISTS ${LLDIR}/lib/libMLIRIR.a)
            set(MLIR_DIR ${LLDIR}/lib/cmake/mlir)
            list(INSERT CMAKE_PREFIX_PATH 0 "${MLIR_DIR}")
        else()
            message(SEND_ERROR "MLIR requested but not found")
        endif()
      else()
        if (EXISTS ${LLVM_DIR}/lib/libMLIRIR.a)
          set(MLIR_DIR ${LLVM_DIR})
          list(INSERT CMAKE_PREFIX_PATH 0 "${MLIR_DIR}")
        else()
            message(SEND_ERROR "MLIR requested but not found")
        endif()
      endif()
      message("mlir dir from llvm ${MLIR_DIR}")
    endif()
endif()

if(RAPTOR_IFX)
    add_definitions(-DRAPTOR_IFX=1)
    message("Building with support for the Intel Fortran compiler IFX")
endif()

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

message("clang dir ${Clang_DIR}")
message("mlir dir ${MLIR_DIR}")

if (DEFINED Clang_DIR)
    find_package(Clang REQUIRED CONFIG PATHS ${Clang_DIR} NO_DEFAULT_PATH)
if (${Clang_FOUND})
    include_directories(${CLANG_INCLUDE_DIRS})
    message("clang inc dir ${CLANG_INCLUDE_DIRS}")
    add_definitions(${CLANG_DEFINITIONS})
endif()
else()
    set(Clang_FOUND 0) 
endif()
message("found Clang ${Clang_FOUND}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
message("LLVM_INSTALL_PREFIX: ${LLVM_INSTALL_PREFIX}")
message("LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message("found LLVM definitions " ${LLVM_DEFINITIONS})
message("found LLVM version " ${LLVM_VERSION_MAJOR})

if(LLVM_VERSION_MAJOR LESS 15)
  message(FATAL_ERROR "LLVM version < 15 is not supported")
endif()

option(RAPTOR_FLANG_VERSION "Build for non-version compliant FLANG" OFF)
if (RAPTOR_FLANG_VERSION)
  add_definitions(-DFLANG=1)
endif()

option(RAPTOR_ROCM_VERSION "Build for non-version compliant ROCM" OFF)
if (RAPTOR_ROCM_VERSION)
  add_definitions(-DROCM=1)
endif()

# Offer the user the choice of overriding the installation directories
set(INSTALL_INCLUDE_DIR include CACHE PATH "Installation directory for header files")
if(WIN32 AND NOT CYGWIN)
  set(DEF_INSTALL_CMAKE_DIR CMake)
else()
  set(DEF_INSTALL_CMAKE_DIR lib/cmake/Raptor)
endif()
set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH
  "Installation directory for CMake files")


find_library(MPFR_LIB_PATH mpfr)
CHECK_INCLUDE_FILE("mpfr.h" HAS_MPFR_H)
message("MPFR lib: " ${MPFR_LIB_PATH})
message("MPFR header: " ${HAS_MPFR_H})

include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")

add_subdirectory(pass)
#add_subdirectory(runtime)

# The benchmarks data are not in git-exported source archives to minimize size.
# Only add the benchmarks if the directory exists.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")
    add_subdirectory(benchmarks)
endif()


# Make relative paths absolute (needed later on)
foreach(p LIB BIN INCLUDE CMAKE)
set(var INSTALL_${p}_DIR)
if(NOT IS_ABSOLUTE "${${var}}")
  set(${var} "${CMAKE_INSTALL_PREFIX}/${${var}}")
endif()
endforeach()

export(TARGETS LLVMRaptor-${LLVM_VERSION_MAJOR}
  FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")

if (${Clang_FOUND})
export(TARGETS ClangRaptor-${LLVM_VERSION_MAJOR}
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")
export(TARGETS ClangRaptorFlags
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")

set(RAPTOR_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}/include/raptor/fprt/flops.def"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/raptor/fprt/fprt.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/raptor/fprt/mpfr.h"
)

set(bundled_includes "${CMAKE_CURRENT_BINARY_DIR}/Raptor/Clang/bundled_includes.h")
list(APPEND RAPTOR_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle-includes.sh)
add_custom_command(OUTPUT ${bundled_includes}
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle-includes.sh
  ${CMAKE_CURRENT_SOURCE_DIR}/include/raptor/raptor
  ${bundled_includes}
  DEPENDS ${RAPTOR_INCLUDES}
  COMMENT "Bundling includes"
  VERBATIM
)
add_custom_target(bundled_includes_target DEPENDS ${bundled_includes})
add_dependencies(ClangRaptor-${LLVM_VERSION_MAJOR} bundled_includes_target)
target_include_directories(ClangRaptor-${LLVM_VERSION_MAJOR} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Raptor/Clang/")
endif()

if (${RAPTOR_FLANG} AND ${Clang_FOUND})
	add_custom_target(link_target ALL
  COMMAND ${CMAKE_COMMAND} -E create_symlink
	  ClangRaptor-${LLVM_VERSION_MAJOR}${LLVM_SHLIBEXT}
	  ${PROJECT_BINARY_DIR}/Raptor/FlangRaptor-${LLVM_VERSION_MAJOR}${LLVM_SHLIBEXT}
  )
endif()

export(TARGETS LLDRaptor-${LLVM_VERSION_MAJOR}
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")

export(TARGETS LLDRaptorFlags
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")
export(TARGETS LLDRaptorAssumeUnknownNoFree
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")
export(TARGETS LLDRaptorLooseTypeFlags
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")
export(TARGETS LLDRaptorPrintTypeFlags
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")
export(TARGETS LLDRaptorPrintFlags
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")
export(TARGETS LLDRaptorNoStrictAliasingFlags
    APPEND FILE "${PROJECT_BINARY_DIR}/RaptorTargets.cmake")

export(PACKAGE Raptor)

set(CONF_LLVM_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
set(CONF_LLVM_VERSION_MINOR ${LLVM_VERSION_MINOR})
set(CONF_LLVM_VERSION_PATCH ${LLVM_VERSION_PATCH})
set(CONF_LLVM_DIR ${LLVM_DIR})
set(CONF_LLVM_BINARY_DIR ${LLVM_BINARY_DIR})
if (${Clang_FOUND})
    get_property(CONF_CLANG_EXE TARGET clang PROPERTY LOCATION)
else()
    set(CONF_CLANG_EXE "")
endif()

set(CONF_INCLUDE_DIRS lib)
configure_file(cmake/RaptorConfig.cmake.in
     "${PROJECT_BINARY_DIR}/RaptorConfig.cmake" @ONLY)
   
set(CONF_INCLUDE_DIRS lib)
configure_file(cmake/RaptorConfig.cmake.in
     "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/RaptorConfig.cmake" @ONLY)

configure_file(cmake/RaptorConfigVersion.cmake.in
     "${PROJECT_BINARY_DIR}/RaptorConfigVersion.cmake" @ONLY)
   
install(FILES
     "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/RaptorConfig.cmake"
     "${PROJECT_BINARY_DIR}/RaptorConfigVersion.cmake"
     DESTINATION "${INSTALL_CMAKE_DIR}" COMPONENT dev)
   
install(EXPORT RaptorTargets
  DESTINATION "${INSTALL_CMAKE_DIR}"
  COMPONENT   dev)

add_subdirectory(test)
