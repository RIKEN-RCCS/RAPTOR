//===- RaptorLogic.h - Implementation of forward and reverse pass generation==//
//
//                             Raptor Project
//
// Part of the Raptor Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#ifndef RAPTOR_LOGIC_H
#define RAPTOR_LOGIC_H

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/Analysis/AliasAnalysis.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"

extern "C" {
extern llvm::cl::opt<bool> RaptorPrint;
extern llvm::cl::opt<bool> RaptorJuliaAddrLoad;
}

constexpr char RaptorFPRTPrefix[] = "__raptor_fprt_";
constexpr char RaptorFPRTOriginalPrefix[] = "__raptor_fprt_original_";

constexpr unsigned F64Width = 64;
constexpr unsigned F64Exponent = 11;
constexpr unsigned F64Significand = 52;
static_assert(F64Width == F64Exponent + F64Significand + 1);
constexpr unsigned F32Width = 32;
constexpr unsigned F32Exponent = 8;
constexpr unsigned F32Significand = 23;
static_assert(F32Width == F32Exponent + F32Significand + 1);
constexpr unsigned F16Width = 16;
constexpr unsigned F16Exponent = 5;
constexpr unsigned F16Significand = 10;
static_assert(F16Width == F16Exponent + F16Significand + 1);

// Holder class to represent a context in which a derivative
// or batch is being requested. This contains the instruction
// (or null) that led to the request, and a builder (or null)
// of the insertion point for code.
struct RequestContext {
  llvm::Instruction *req;
  llvm::IRBuilder<> *ip;
  RequestContext(llvm::Instruction *req = nullptr,
                 llvm::IRBuilder<> *ip = nullptr)
      : req(req), ip(ip) {}
};

[[maybe_unused]] static llvm::Type *
getTypeForWidth(llvm::LLVMContext &ctx, unsigned width, bool builtinFloat) {
  switch (width) {
  default:
    if (builtinFloat)
      llvm::report_fatal_error("Invalid float width requested");
    else
      llvm::report_fatal_error(
          "Truncation to non builtin float width unsupported");
  case F64Width:
    return llvm::Type::getDoubleTy(ctx);
  case F32Width:
    return llvm::Type::getFloatTy(ctx);
  case F16Width:
    return llvm::Type::getHalfTy(ctx);
  }
}

enum TruncateMode {
  TruncMemMode = 0b0001,
  TruncOpMode = 0b0010,
  TruncOpFullModuleMode = 0b0110,
  TruncCountMode = 0b1000,
};
[[maybe_unused]] static const char *truncateModeStr(TruncateMode mode) {
  switch (mode) {
  case TruncMemMode:
    return "mem";
  case TruncOpMode:
    return "op";
  case TruncOpFullModuleMode:
    return "op_full_module";
  case TruncCountMode:
    return "count";
  }
  llvm_unreachable("Invalid truncation mode");
}

struct FloatRepresentation {
public:
  enum FloatRepresentationType { IEEE = 0, MPFR = 1 };

private:
  FloatRepresentation() {}

  // |_|__________|_________________|
  //  ^         ^         ^
  //  sign bit  exponent  significand
  //
  //  value = (sign) * significand * 2 ^ exponent
  unsigned ExponentWidth;
  unsigned SignificandWidth;
  FloatRepresentationType Ty;

public:
  FloatRepresentation(FloatRepresentation &) = default;
  FloatRepresentation(const FloatRepresentation &) = default;
  ~FloatRepresentation() {}

  static FloatRepresentation getMPFR(unsigned E, unsigned S) {
    FloatRepresentation Repr;
    Repr.ExponentWidth = E;
    Repr.SignificandWidth = S;
    Repr.Ty = MPFR;
    return Repr;
  }

  static std::optional<FloatRepresentation>
  getFromString(llvm::StringRef &ConfigStr) {
    if (!ConfigStr.consume_front("ieee(")) {
      unsigned Width = 0;
      if (ConfigStr.consumeInteger(10, Width))
        return {};
      if (ConfigStr.consume_front(")"))
        return {};
      return getIEEE(Width);
    } else if (!ConfigStr.consume_front("mpfr(")) {
      unsigned Exponent = 0;
      unsigned Significand = 0;
      if (ConfigStr.consumeInteger(10, Exponent))
        return {};
      if (ConfigStr.consume_front(","))
        return {};
      if (ConfigStr.consumeInteger(10, Significand))
        return {};
      if (ConfigStr.consume_front(")"))
        return {};
      return getMPFR(Exponent, Significand);
    }
    return {};
  }

  static FloatRepresentation getIEEE(unsigned width) {
    FloatRepresentation Repr;
    switch (width) {
    case F64Width:
      Repr.ExponentWidth = F64Exponent;
      Repr.SignificandWidth = F64Significand;
      break;
    case F32Width:
      Repr.ExponentWidth = F32Exponent;
      Repr.SignificandWidth = F32Significand;
      break;
    case F16Width:
      Repr.ExponentWidth = F16Exponent;
      Repr.SignificandWidth = F16Significand;
      break;
    default:
      llvm_unreachable("Invalid IEEE width");
    }
    assert(width == Repr.ExponentWidth + Repr.SignificandWidth + 1);
    assert(width == Repr.getWidth());
    Repr.Ty = IEEE;
    return Repr;
  }

  FloatRepresentationType getType() const { return Ty; }

  unsigned getWidth() const { return 1 + ExponentWidth + SignificandWidth; }

  unsigned getExponentWidth() const { return ExponentWidth; }

  unsigned getSignificandWidth() const { return SignificandWidth; }

  bool isIEEE() { return Ty == IEEE; }
  bool isMPFR() { return Ty == MPFR; }

  bool canBeBuiltin() const {
    unsigned w = getWidth();
    return (w == F16Width && SignificandWidth == F16Significand) ||
           (w == F32Width && SignificandWidth == F32Significand) ||
           (w == F64Width && SignificandWidth == F64Significand);
  }

  llvm::Type *getBuiltinType(llvm::LLVMContext &ctx) const {
    if (!canBeBuiltin())
      return nullptr;
    return getTypeForWidth(ctx, getWidth(), /*builtinFloat=*/true);
  }

  llvm::Type *getType(llvm::LLVMContext &ctx) const {
    llvm::Type *builtinType = getBuiltinType(ctx);
    if (builtinType)
      return builtinType;
    llvm_unreachable("TODO MPFR");
  }

  bool operator==(const FloatRepresentation &Other) const {
    return Other.Ty == Ty && Other.ExponentWidth == ExponentWidth &&
           Other.SignificandWidth == SignificandWidth;
  }

  bool operator<(const FloatRepresentation &Other) const {
    return std::tuple(Ty, ExponentWidth, SignificandWidth) <
           std::tuple(Other.Ty, Other.ExponentWidth, Other.SignificandWidth);
  }

  std::string getMangling() const {
    switch (Ty) {
    case IEEE:
      return "ieee_" + std::to_string(getWidth());
    case MPFR:
      return "mpfr_" + std::to_string(getExponentWidth()) + "_" +
             std::to_string(getSignificandWidth());
    default:
      llvm_unreachable("Unknown type");
    }
  }
};

struct FloatTruncation {
private:
  FloatRepresentation From, To;
  TruncateMode Mode;

public:
  FloatTruncation(FloatRepresentation From, TruncateMode Mode)
      : From(From), To(From), Mode(Mode) {
    if (Mode != TruncCountMode)
      llvm::report_fatal_error("Only count mode allowed in this constructor");
  }
  FloatTruncation(FloatRepresentation From, FloatRepresentation To,
                  TruncateMode mode)
      : From(From), To(To), Mode(mode) {
    if (!From.isIEEE())
      llvm::report_fatal_error("Float truncation `from` type is not IEEE.");
    if (!From.canBeBuiltin())
      llvm::report_fatal_error("Float truncation `from` type is not builtin.");
    // TODO make these warnings
    // if (From.exponentWidth < To.exponentWidth &&
    //     (mode == TruncOpMode || mode == TruncOpFullModuleMode))
    //   llvm::report_fatal_error("Float truncation `from` type must have "
    //                            "a wider exponent than `to`.");
    // if (From.significandWidth < To.significandWidth &&
    //     (mode == TruncOpMode || mode == TruncOpFullModuleMode))
    //   llvm::report_fatal_error("Float truncation `from` type must have "
    //                            "a wider significand than `to`.");
    // if (From == To)
    //   llvm::report_fatal_error(
    //       "Float truncation `from` and `to` type must not be the same.");
  }
  TruncateMode getMode() { return Mode; }
  FloatRepresentation getTo() { return To; }
  FloatRepresentation getFrom() { return From; }
  unsigned getFromTypeWidth() { return From.getWidth(); }
  unsigned getToTypeWidth() { return To.getWidth(); }
  llvm::Type *getFromType(llvm::LLVMContext &ctx) {
    return From.getBuiltinType(ctx);
  }
  bool isToFPRT() {
    // TODO maybe add new mode in which we directly truncate to native fp ops,
    // for now everything goes through the runtime
    return true;
  }
  llvm::Type *getToType(llvm::LLVMContext &ctx) { return getFromType(ctx); }
  auto getTuple() const { return std::tuple(From, To, Mode); }
  bool operator==(const FloatTruncation &other) const {
    return getTuple() == other.getTuple();
  }
  bool operator<(const FloatTruncation &other) const {
    return getTuple() < other.getTuple();
  }
  std::string mangleTruncation() const {
    if (Mode == TruncCountMode)
      return "count";
    return From.getMangling() + "_to_" + To.getMangling();
  }
  std::string mangleFrom() const { return From.getMangling(); }
};

typedef std::map<std::tuple<std::string, unsigned, unsigned>,
                 llvm::GlobalValue *>
    UniqDebugLocStrsTy;

class RaptorLogic {
public:
  UniqDebugLocStrsTy UniqDebugLocStrs;

  /// \p PostOpt is whether to perform basic
  ///  optimization of the function after synthesis
  bool PostOpt;

  RaptorLogic(bool PostOpt) : PostOpt(PostOpt) {}

  using TruncateCacheKey =
      std::tuple<llvm::Function *, FloatTruncation, unsigned, bool>;
  std::map<TruncateCacheKey, llvm::Function *> TruncateCachedFunctions;
  llvm::Function *CreateTruncateFunc(RequestContext context,
                                     llvm::Function *tobatch,
                                     FloatTruncation truncation,
                                     TruncateMode mode, bool root = true);
  bool CreateTruncateValue(RequestContext context, llvm::Value *addr,
                           FloatTruncation Truncation, bool isTruncate);

  void clear();
};

extern "C" {
extern llvm::cl::opt<bool> looseTypeAnalysis;
extern llvm::cl::opt<bool> nonmarkedglobals_inactiveloads;
};

#endif
