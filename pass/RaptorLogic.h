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
#include "llvm/Support/ErrorHandling.h"

extern "C" {
extern llvm::cl::opt<bool> RaptorPrint;
extern llvm::cl::opt<bool> RaptorJuliaAddrLoad;
}

constexpr char RaptorFPRTPrefix[] = "__raptor_fprt_";
constexpr char RaptorFPRTOriginalPrefix[] = "__raptor_fprt_original_";

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
  case 64:
    return llvm::Type::getDoubleTy(ctx);
  case 32:
    return llvm::Type::getFloatTy(ctx);
  case 16:
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
  // |_|__________|_________________|
  //  ^         ^         ^
  //  sign bit  exponent  significand
  //
  //  value = (sign) * significand * 2 ^ exponent
  unsigned exponentWidth;
  unsigned significandWidth;

  FloatRepresentation() : exponentWidth(0), significandWidth(0) {}
  FloatRepresentation(unsigned e, unsigned s)
      : exponentWidth(e), significandWidth(s) {}

  unsigned getTypeWidth() const { return 1 + exponentWidth + significandWidth; }

  bool canBeBuiltin() const {
    unsigned w = getTypeWidth();
    return (w == 16 && significandWidth == 10) ||
           (w == 32 && significandWidth == 23) ||
           (w == 64 && significandWidth == 52);
  }

  llvm::Type *getBuiltinType(llvm::LLVMContext &ctx) const {
    if (!canBeBuiltin())
      return nullptr;
    return getTypeForWidth(ctx, getTypeWidth(), /*builtinFloat=*/true);
  }

  llvm::Type *getType(llvm::LLVMContext &ctx) const {
    llvm::Type *builtinType = getBuiltinType(ctx);
    if (builtinType)
      return builtinType;
    llvm_unreachable("TODO MPFR");
  }

  bool operator==(const FloatRepresentation &other) const {
    return other.exponentWidth == exponentWidth &&
           other.significandWidth == significandWidth;
  }
  bool operator<(const FloatRepresentation &other) const {
    return std::tuple(exponentWidth, significandWidth) <
           std::tuple(other.exponentWidth, other.significandWidth);
  }
  std::string to_string() const {
    return std::to_string(getTypeWidth()) + "_" +
           std::to_string(significandWidth);
  }
};

struct FloatTruncation {
private:
  FloatRepresentation from, to;
  TruncateMode mode;

public:
  FloatTruncation(FloatRepresentation From, TruncateMode mode)
      : from(From), mode(mode) {
    if (mode != TruncCountMode)
      llvm::report_fatal_error("Only count mode allowed in this constructor");
  }
  FloatTruncation(FloatRepresentation From, FloatRepresentation To,
                  TruncateMode mode)
      : from(From), to(To), mode(mode) {
    if (!From.canBeBuiltin())
      llvm::report_fatal_error("Float truncation `from` type is not builtin.");
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
  TruncateMode getMode() { return mode; }
  FloatRepresentation getTo() { return to; }
  unsigned getFromTypeWidth() { return from.getTypeWidth(); }
  unsigned getToTypeWidth() { return to.getTypeWidth(); }
  llvm::Type *getFromType(llvm::LLVMContext &ctx) {
    return from.getBuiltinType(ctx);
  }
  bool isToFPRT() {
    // TODO maybe add new mode in which we directly truncate to native fp ops,
    // for now everything goes through the runtime
    return true;
  }
  llvm::Type *getToType(llvm::LLVMContext &ctx) { return getFromType(ctx); }
  auto getTuple() const { return std::tuple(from, to, mode); }
  bool operator==(const FloatTruncation &other) const {
    return getTuple() == other.getTuple();
  }
  bool operator<(const FloatTruncation &other) const {
    return getTuple() < other.getTuple();
  }
  std::string mangleTruncation() const {
    if (mode == TruncCountMode)
      return "count";
    return from.to_string() + "to" + to.to_string();
  }
  std::string mangleFrom() const { return from.to_string(); }
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
                           FloatRepresentation from, FloatRepresentation to,
                           bool isTruncate);

  void clear();
};

extern "C" {
extern llvm::cl::opt<bool> looseTypeAnalysis;
extern llvm::cl::opt<bool> nonmarkedglobals_inactiveloads;
};

#endif
