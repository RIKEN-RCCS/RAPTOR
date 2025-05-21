//===- Utils.h - Declaration of miscellaneous utilities -------------------===//
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

#ifndef RAPTOR_UTILS_H
#define RAPTOR_UTILS_H

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"

extern const llvm::StringMap<llvm::Intrinsic::ID> LIBM_FUNCTIONS;

extern "C" {
/// Print additional debug info relevant to performance
extern llvm::cl::opt<bool> RaptorPrintPerf;
}

template <typename... Args>
void EmitWarning(llvm::StringRef RemarkName,
                 const llvm::DiagnosticLocation &Loc,
                 const llvm::BasicBlock *BB, const Args &...args) {

  llvm::LLVMContext &Ctx = BB->getContext();
  if (Ctx.getDiagHandlerPtr()->isPassedOptRemarkEnabled("raptor")) {
    std::string str;
    llvm::raw_string_ostream ss(str);
    (ss << ... << args);
    auto R = llvm::OptimizationRemark("raptor", RemarkName, Loc, BB)
             << ss.str();
    Ctx.diagnose(R);
  }

  if (RaptorPrintPerf)
    (llvm::errs() << ... << args) << "\n";
}

template <typename... Args>
void EmitWarning(llvm::StringRef RemarkName, const llvm::Instruction &I,
                 const Args &...args) {
  EmitWarning(RemarkName, I.getDebugLoc(), I.getParent(), args...);
}

template <typename... Args>
void EmitWarning(llvm::StringRef RemarkName, const llvm::Function &F,
                 const Args &...args) {
  llvm::LLVMContext &Ctx = F.getContext();
  if (Ctx.getDiagHandlerPtr()->isPassedOptRemarkEnabled("raptor")) {
    std::string str;
    llvm::raw_string_ostream ss(str);
    (ss << ... << args);
    auto R = llvm::OptimizationRemark("raptor", RemarkName, &F) << ss.str();
    Ctx.diagnose(R);
  }
  if (RaptorPrintPerf)
    (llvm::errs() << ... << args) << "\n";
}

class RaptorFailure final : public llvm::DiagnosticInfoUnsupported {
public:
  RaptorFailure(const llvm::Twine &Msg, const llvm::DiagnosticLocation &Loc,
                const llvm::Instruction *CodeRegion);
  RaptorFailure(const llvm::Twine &Msg, const llvm::DiagnosticLocation &Loc,
                const llvm::Function *CodeRegion);
};

template <typename... Args>
void EmitFailure(llvm::StringRef RemarkName,
                 const llvm::DiagnosticLocation &Loc,
                 const llvm::Instruction *CodeRegion, Args &...args) {
  std::string *str = new std::string();
  llvm::raw_string_ostream ss(*str);
  (ss << ... << args);
  CodeRegion->getContext().diagnose(
      (RaptorFailure("Raptor: " + ss.str(), Loc, CodeRegion)));
}

llvm::CallInst *createIntrinsicCall(llvm::IRBuilderBase &B,
                                    llvm::Intrinsic::ID ID, llvm::Type *RetTy,
                                    llvm::ArrayRef<llvm::Value *> Args,
                                    llvm::Instruction *FMFSource = nullptr,
                                    const llvm::Twine &Name = "");

template <typename T> static inline llvm::Function *getFunctionFromCall(T *op) {
  const llvm::Function *called = nullptr;
  using namespace llvm;
  const llvm::Value *callVal;
  callVal = op->getCalledOperand();
  while (!called) {
    if (auto castinst = dyn_cast<ConstantExpr>(callVal))
      if (castinst->isCast()) {
        callVal = castinst->getOperand(0);
        continue;
      }
    if (auto fn = dyn_cast<Function>(callVal)) {
      called = fn;
      break;
    }
    if (auto alias = dyn_cast<GlobalAlias>(callVal)) {
      callVal = alias->getAliasee();
      continue;
    }
    break;
  }
  return called ? const_cast<llvm::Function *>(called) : nullptr;
}

static inline llvm::StringRef getFuncName(llvm::Function *called) {
  if (called->hasFnAttribute("raptor_math"))
    return called->getFnAttribute("raptor_math").getValueAsString();
  else if (called->hasFnAttribute("raptor_allocator"))
    return "raptor_allocator";
  else
    return called->getName();
}

static inline llvm::StringRef getFuncNameFromCall(const llvm::CallBase *op) {
  auto AttrList =
      op->getAttributes().getAttributes(llvm::AttributeList::FunctionIndex);
  if (AttrList.hasAttribute("raptor_math"))
    return AttrList.getAttribute("raptor_math").getValueAsString();
  if (AttrList.hasAttribute("raptor_allocator"))
    return "raptor_allocator";

  if (auto called = getFunctionFromCall(op)) {
    return getFuncName(called);
  }
  return "";
}

static inline bool endsWith(llvm::StringRef string, llvm::StringRef suffix) {
#if LLVM_VERSION_MAJOR >= 18
  return string.ends_with(suffix);
#else
  return string.endswith(suffix);
#endif // LLVM_VERSION_MAJOR
}

static inline bool startsWith(llvm::StringRef string, llvm::StringRef prefix) {
#if LLVM_VERSION_MAJOR >= 18
  return string.starts_with(prefix);
#else
  return string.startswith(prefix);
#endif // LLVM_VERSION_MAJOR
}

static inline bool isMemFreeLibMFunction(llvm::StringRef str,
                                         llvm::Intrinsic::ID *ID = nullptr) {
  if (startsWith(str, "__") && endsWith(str, "_finite")) {
    str = str.substr(2, str.size() - 2 - 7);
  } else if (startsWith(str, "__fd_") && endsWith(str, "_1")) {
    str = str.substr(5, str.size() - 5 - 2);
  } else if (startsWith(str, "__nv_")) {
    str = str.substr(5, str.size() - 5);
  }
  if (LIBM_FUNCTIONS.find(str.str()) != LIBM_FUNCTIONS.end()) {
    if (ID)
      *ID = LIBM_FUNCTIONS.find(str.str())->second;
    return true;
  }
  if (endsWith(str, "f") || endsWith(str, "l")) {
    if (LIBM_FUNCTIONS.find(str.substr(0, str.size() - 1).str()) !=
        LIBM_FUNCTIONS.end()) {
      if (ID)
        *ID = LIBM_FUNCTIONS.find(str.substr(0, str.size() - 1).str())->second;
      return true;
    }
  }
  return false;
}



#endif // RAPTOR_UTILS
