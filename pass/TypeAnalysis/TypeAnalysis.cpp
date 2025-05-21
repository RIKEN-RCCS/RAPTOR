//===- TypeAnalysis.cpp - Implementation of Type Analysis   ------------===//
//
//                             Enzyme Project
//
// Part of the Enzyme Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"

#include <llvm/Config/llvm-config.h>

#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/ItaniumDemangle.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include <math.h>


using namespace llvm;

const llvm::StringMap<llvm::Intrinsic::ID> LIBM_FUNCTIONS = {
    {"sinc", Intrinsic::not_intrinsic},
    {"sincn", Intrinsic::not_intrinsic},
    {"cos", Intrinsic::cos},
    {"sin", Intrinsic::sin},
    {"tan", Intrinsic::not_intrinsic},
    {"acos", Intrinsic::not_intrinsic},
    {"__nv_frcp_rd", Intrinsic::not_intrinsic},
    {"__nv_frcp_rn", Intrinsic::not_intrinsic},
    {"__nv_frcp_ru", Intrinsic::not_intrinsic},
    {"__nv_frcp_rz", Intrinsic::not_intrinsic},
    {"__nv_drcp_rd", Intrinsic::not_intrinsic},
    {"__nv_drcp_rn", Intrinsic::not_intrinsic},
    {"__nv_drcp_ru", Intrinsic::not_intrinsic},
    {"__nv_drcp_rz", Intrinsic::not_intrinsic},
    {"__nv_isnand", Intrinsic::not_intrinsic},
    {"__nv_isnanf", Intrinsic::not_intrinsic},
    {"__nv_isinfd", Intrinsic::not_intrinsic},
    {"__nv_isinff", Intrinsic::not_intrinsic},
    {"__nv_acos", Intrinsic::not_intrinsic},
    {"asin", Intrinsic::not_intrinsic},
    {"__nv_asin", Intrinsic::not_intrinsic},
    {"atan", Intrinsic::not_intrinsic},
    {"atan2", Intrinsic::not_intrinsic},
    {"__nv_atan2", Intrinsic::not_intrinsic},
    {"cosh", Intrinsic::not_intrinsic},
    {"sinh", Intrinsic::not_intrinsic},
    {"tanh", Intrinsic::not_intrinsic},
    {"acosh", Intrinsic::not_intrinsic},
    {"asinh", Intrinsic::not_intrinsic},
    {"atanh", Intrinsic::not_intrinsic},
    {"exp", Intrinsic::exp},
    {"exp2", Intrinsic::exp2},
    {"exp10", Intrinsic::not_intrinsic},
    {"log", Intrinsic::log},
    {"log10", Intrinsic::log10},
    {"expm1", Intrinsic::not_intrinsic},
    {"log1p", Intrinsic::not_intrinsic},
    {"log2", Intrinsic::log2},
    {"logb", Intrinsic::not_intrinsic},
    {"pow", Intrinsic::pow},
    {"sqrt", Intrinsic::sqrt},
    {"cbrt", Intrinsic::not_intrinsic},
    {"hypot", Intrinsic::not_intrinsic},

    {"__mulsc3", Intrinsic::not_intrinsic},
    {"__muldc3", Intrinsic::not_intrinsic},
    {"__multc3", Intrinsic::not_intrinsic},
    {"__mulxc3", Intrinsic::not_intrinsic},

    {"__divsc3", Intrinsic::not_intrinsic},
    {"__divdc3", Intrinsic::not_intrinsic},
    {"__divtc3", Intrinsic::not_intrinsic},
    {"__divxc3", Intrinsic::not_intrinsic},

    {"Faddeeva_erf", Intrinsic::not_intrinsic},
    {"Faddeeva_erfc", Intrinsic::not_intrinsic},
    {"Faddeeva_erfcx", Intrinsic::not_intrinsic},
    {"Faddeeva_erfi", Intrinsic::not_intrinsic},
    {"Faddeeva_dawson", Intrinsic::not_intrinsic},
    {"Faddeeva_erf_re", Intrinsic::not_intrinsic},
    {"Faddeeva_erfc_re", Intrinsic::not_intrinsic},
    {"Faddeeva_erfcx_re", Intrinsic::not_intrinsic},
    {"Faddeeva_erfi_re", Intrinsic::not_intrinsic},
    {"Faddeeva_dawson_re", Intrinsic::not_intrinsic},
    {"erf", Intrinsic::not_intrinsic},
    {"erfi", Intrinsic::not_intrinsic},
    {"erfc", Intrinsic::not_intrinsic},

    {"__fd_sincos_1", Intrinsic::not_intrinsic},
    {"sincospi", Intrinsic::not_intrinsic},
    {"cmplx_inv", Intrinsic::not_intrinsic},

    // bessel functions
    {"j0", Intrinsic::not_intrinsic},
    {"j1", Intrinsic::not_intrinsic},
    {"jn", Intrinsic::not_intrinsic},
    {"y0", Intrinsic::not_intrinsic},
    {"y1", Intrinsic::not_intrinsic},
    {"yn", Intrinsic::not_intrinsic},
    {"tgamma", Intrinsic::not_intrinsic},
    {"lgamma", Intrinsic::not_intrinsic},
    {"logabsgamma", Intrinsic::not_intrinsic},
    {"ceil", Intrinsic::ceil},
    {"__nv_ceil", Intrinsic::ceil},
    {"floor", Intrinsic::floor},
    {"fmod", Intrinsic::not_intrinsic},
    {"trunc", Intrinsic::trunc},
    {"round", Intrinsic::round},
    {"rint", Intrinsic::rint},
    {"nearbyint", Intrinsic::nearbyint},
    {"remainder", Intrinsic::not_intrinsic},
    {"copysign", Intrinsic::copysign},
    {"nextafter", Intrinsic::not_intrinsic},
    {"nexttoward", Intrinsic::not_intrinsic},
    {"fdim", Intrinsic::not_intrinsic},
    {"fmax", Intrinsic::maxnum},
    {"fmin", Intrinsic::minnum},
    {"fabs", Intrinsic::fabs},
    {"fma", Intrinsic::fma},
    {"ilogb", Intrinsic::not_intrinsic},
    {"scalbn", Intrinsic::not_intrinsic},
    {"scalbln", Intrinsic::not_intrinsic},
    {"powi", Intrinsic::powi},
    {"cabs", Intrinsic::not_intrinsic},
    {"ldexp", Intrinsic::not_intrinsic},
    {"fmod", Intrinsic::not_intrinsic},
    {"finite", Intrinsic::not_intrinsic},
    {"isinf", Intrinsic::not_intrinsic},
    {"isnan", Intrinsic::not_intrinsic},
    {"lround", Intrinsic::lround},
    {"llround", Intrinsic::llround},
    {"lrint", Intrinsic::lrint},
    {"llrint", Intrinsic::llrint}};
