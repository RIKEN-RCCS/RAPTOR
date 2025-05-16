#if(WIN32 OR CYGWIN)
#  set(LLVM_LINK_COMPONENTS Core Support)
#endif()

get_target_property(TBL_LINKED_LIBS LLVMSupport INTERFACE_LINK_LIBRARIES)
if (NOT TBL_LINKED_LIBS)
else()
list(REMOVE_ITEM TBL_LINKED_LIBS "ZLIB::ZLIB")
set_property(TARGET LLVMSupport PROPERTY INTERFACE_LINK_LIBRARIES ${TBL_LINKED_LIBS})
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_LINK_COMPONENTS Demangle)

set(RAPTOR_SRC
    RaptorLogic.cpp
    Raptor.cpp
    Utils.cpp
    TypeAnalysis/TypeAnalysis.cpp
)
list(REMOVE_ITEM RAPTOR_SRC "eopt.cpp")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# on windows `PLUGIN_TOOL` doesn't link against LLVM.dll
if ((WIN32 OR CYGWIN) AND LLVM_LINK_LLVM_DYLIB)
    add_llvm_library( LLVMRaptor-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC}
        PARTIAL_SOURCES_INTENDED
        MODULE
        DEPENDS
        intrinsics_gen
	LINK_COMPONENTS
	LLVM
    )
if (${Clang_FOUND})
    add_llvm_library( ClangRaptor-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC} Clang/RaptorClang.cpp
        Clang/RaptorPassLoader.cpp
        PARTIAL_SOURCES_INTENDED
        MODULE
        DEPENDS
        intrinsics_gen
	LINK_COMPONENTS
	LLVM
    )
target_compile_definitions(ClangRaptor-${LLVM_VERSION_MAJOR} PUBLIC RAPTOR_RUNPASS)
endif()
        add_llvm_library( LLDRaptor-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC} Clang/RaptorPassLoader.cpp
        PARTIAL_SOURCES_INTENDED
        MODULE
        DEPENDS
        intrinsics_gen
	LINK_COMPONENTS
	LLVM
    )
target_compile_definitions(LLDRaptor-${LLVM_VERSION_MAJOR} PUBLIC RAPTOR_RUNPASS)
else()
    add_llvm_library( LLVMRaptor-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC}
        PARTIAL_SOURCES_INTENDED
        MODULE
        DEPENDS
        intrinsics_gen
        PLUGIN_TOOL
        opt
      )
target_compile_definitions(LLVMRaptor-${LLVM_VERSION_MAJOR} PUBLIC RAPTOR_RUNPASS)
if (${Clang_FOUND})
    add_llvm_library( ClangRaptor-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC} Clang/RaptorClang.cpp
        Clang/RaptorPassLoader.cpp
        PARTIAL_SOURCES_INTENDED
        MODULE
        DEPENDS
        intrinsics_gen
        PLUGIN_TOOL
        clang
    )
target_compile_definitions(ClangRaptor-${LLVM_VERSION_MAJOR} PUBLIC RAPTOR_RUNPASS)
endif()
    add_llvm_library( LLDRaptor-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC} Clang/RaptorPassLoader.cpp
        PARTIAL_SOURCES_INTENDED
        MODULE
        DEPENDS
        intrinsics_gen
        PLUGIN_TOOL
        opt
    )
target_compile_definitions(LLDRaptor-${LLVM_VERSION_MAJOR} PUBLIC RAPTOR_RUNPASS)
endif()

if (${RAPTOR_STATIC_LIB})
    add_llvm_library( RaptorStatic-${LLVM_VERSION_MAJOR}
        ${RAPTOR_SRC}
        PARTIAL_SOURCES_INTENDED
        STATIC
        DEPENDS
        intrinsics_gen
    )
endif()

if (${RAPTOR_EXTERNAL_SHARED_LIB})
    add_library( Raptor-${LLVM_VERSION_MAJOR}
        SHARED
        ${RAPTOR_SRC}
    )
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} intrinsics_gen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} BinopDerivativesIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} IntrinsicDerivativesIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} CallDerivativesIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} InstructionDerivativesIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} BlasDerivativesIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} BlasDeclarationsIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} BlasTAIncGen)
    add_dependencies(Raptor-${LLVM_VERSION_MAJOR} BlasDiffUseIncGen)
    target_link_libraries(Raptor-${LLVM_VERSION_MAJOR} LLVM)
    install(TARGETS Raptor-${LLVM_VERSION_MAJOR}
        EXPORT RaptorTargets
        LIBRARY DESTINATION lib COMPONENT shlib
        PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/Raptor"
        COMPONENT dev)
endif()

if (APPLE)
# Darwin-specific linker flags for loadable modules.
set_target_properties(LLVMRaptor-${LLVM_VERSION_MAJOR} PROPERTIES
    LINK_FLAGS "-Wl,-flat_namespace -Wl,-undefined -Wl,suppress")
if (${Clang_FOUND})
set_target_properties(ClangRaptor-${LLVM_VERSION_MAJOR} PROPERTIES
        LINK_FLAGS "-Wl,-flat_namespace -Wl,-undefined -Wl,suppress")
endif()
set_target_properties(LLDRaptor-${LLVM_VERSION_MAJOR} PROPERTIES
        LINK_FLAGS "-Wl,-flat_namespace -Wl,-undefined -Wl,suppress")
endif()

add_library(LLDRaptorFlags INTERFACE)
target_compile_options(LLDRaptorFlags INTERFACE -flto)
target_link_options(LLDRaptorFlags INTERFACE "SHELL: -fuse-ld=lld")

target_link_options(LLDRaptorFlags INTERFACE "SHELL: -Wl,-mllvm -Wl,-load=$<TARGET_FILE:LLDRaptor-${LLVM_VERSION_MAJOR}>")
target_link_options(LLDRaptorFlags INTERFACE "SHELL: -Wl,--load-pass-plugin=$<TARGET_FILE:LLDRaptor-${LLVM_VERSION_MAJOR}>")

add_library(LLDRaptorAssumeUnknownNoFree INTERFACE)
target_link_options(LLDRaptorAssumeUnknownNoFree INTERFACE "SHELL: -Wl,-mllvm -Wl,-raptor-assume-unknown-nofree=1")

add_library(LLDRaptorLooseTypeFlags INTERFACE)
target_link_options(LLDRaptorLooseTypeFlags INTERFACE "SHELL: -Wl,-mllvm -Wl,-raptor-loose-types=1")

add_library(LLDRaptorPrintTypeFlags INTERFACE)
target_link_options(LLDRaptorPrintTypeFlags INTERFACE "SHELL: -Wl,-mllvm -Wl,-raptor-print-type=1")

add_library(LLDRaptorPrintFlags INTERFACE)
target_link_options(LLDRaptorPrintFlags INTERFACE "SHELL: -Wl,-mllvm -Wl,-raptor-print=1")

add_library(LLDRaptorNoStrictAliasingFlags INTERFACE)
target_link_options(LLDRaptorNoStrictAliasingFlags INTERFACE "SHELL: -Wl,-mllvm -Wl,-raptor-strict-aliasing=0")

# this custom target exists to prevent CMake from incorrectly assuming that 
# targets that link depend on LLDRaptor-XX can be built at the same time or 
# before LLDRaptor-XX has finished.
add_custom_target(LLDRaptorDummy "" DEPENDS LLDRaptor-${LLVM_VERSION_MAJOR})
add_dependencies(LLDRaptorFlags LLDRaptorDummy)

add_library(ClangRaptorFlags INTERFACE)
target_compile_options(ClangRaptorFlags INTERFACE "-fplugin=$<TARGET_FILE:ClangRaptor-${LLVM_VERSION_MAJOR}>")

# this custom target exists to prevent CMake from incorrectly assuming that 
# targets that link depend on ClangRaptor-XX can be built at the same time or 
# before ClangRaptor-XX has finished.
add_custom_target(ClangRaptorDummy "" DEPENDS ClangRaptor-${LLVM_VERSION_MAJOR})
add_dependencies(ClangRaptorFlags ClangRaptorDummy)

install(TARGETS LLDRaptorFlags EXPORT RaptorTargets)
install(TARGETS ClangRaptorFlags EXPORT RaptorTargets)

install(TARGETS LLVMRaptor-${LLVM_VERSION_MAJOR}
    EXPORT RaptorTargets
    LIBRARY DESTINATION lib COMPONENT shlib
    PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/Raptor"
    COMPONENT dev)

if (${Clang_FOUND})
install(TARGETS ClangRaptor-${LLVM_VERSION_MAJOR}
    EXPORT RaptorTargets
    LIBRARY DESTINATION lib COMPONENT shlib
    PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/Raptor"
    COMPONENT dev)
endif()
install(TARGETS LLDRaptor-${LLVM_VERSION_MAJOR}
    EXPORT RaptorTargets
    LIBRARY DESTINATION lib COMPONENT shlib
    PUBLIC_HEADER DESTINATION "${INSTALL_INCLUDE_DIR}/Raptor"
    COMPONENT dev)

