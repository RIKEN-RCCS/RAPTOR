//===- Raptor.cpp - Automatic Differentiation Transformation Pass  -------===//
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
// This file contains Raptor, a transformation pass that takes replaces calls
// to function calls to *__raptor_autodiff* with a call to the derivative of
// the function passed as the first argument.
//
//===----------------------------------------------------------------------===//
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Pass.h"
#include <memory>

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

#include "llvm/Passes/PassBuilder.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Scalar.h"

#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/AbstractCallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "RaptorLogic.h"
#include "Utils.h"

#include "llvm/Transforms/Utils.h"

#include "llvm/Transforms/IPO/Attributor.h"
#include "llvm/Transforms/IPO/OpenMPOpt.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

using namespace llvm;
#ifdef DEBUG_TYPE
#undef DEBUG_TYPE
#endif
#define DEBUG_TYPE "lower-raptor-intrinsic"

llvm::cl::opt<bool> RaptorEnable("raptor-enable", cl::init(true), cl::Hidden,
                                 cl::desc("Run the Raptor pass"));

llvm::cl::opt<bool>
    RaptorPostOpt("raptor-postopt", cl::init(false), cl::Hidden,
                  cl::desc("Run raptorpostprocessing optimizations"));

llvm::cl::opt<bool> RaptorAttributor("raptor-attributor", cl::init(false),
                                     cl::Hidden,
                                     cl::desc("Run attributor post Raptor"));

llvm::cl::opt<bool> RaptorOMPOpt("raptor-omp-opt", cl::init(false), cl::Hidden,
                                 cl::desc("Whether to enable openmp opt"));

llvm::cl::opt<std::string> RaptorTruncateAll(
    "raptor-truncate-all", cl::init(""), cl::Hidden,
    cl::desc(
        "Truncate all floating point operations. "
        "E.g. \"64to32\" or \"64to<exponent_width>-<significand_width>\"."));

llvm::cl::opt<bool> RaptorTruncateCount(
    "raptor-truncate-count", cl::init(false), cl::Hidden,
    cl::desc("Count all non-truncated floating point operations."));
llvm::cl::opt<bool> RaptorTruncateAccessCount(
    "raptor-truncate-access-count", cl::init(false), cl::Hidden,
    cl::desc("Count all floating-point loads and stores."));

#define addAttribute addAttributeAtIndex
#define getAttribute getAttributeAtIndex
bool attributeKnownFunctions(llvm::Function &F) {
  bool changed = false;
  if (F.getName() == "fprintf") {
    for (auto &arg : F.args()) {
      if (arg.getType()->isPointerTy()) {
        arg.addAttr(Attribute::NoCapture);
        changed = true;
      }
    }
  }
  if (F.getName().contains("__raptor_float") ||
      F.getName().contains("__raptor_double") ||
      F.getName().contains("__raptor_integer") ||
      F.getName().contains("__raptor_pointer") ||
      F.getName().contains("__raptor_todense") ||
      F.getName().contains("__raptor_iter") ||
      F.getName().contains("__raptor_virtualreverse")) {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyReadsMemory();
    F.setOnlyWritesMemory();
#else
    F.addFnAttr(Attribute::ReadNone);
#endif
    if (!F.getName().contains("__raptor_todense"))
      for (auto &arg : F.args()) {
        if (arg.getType()->isPointerTy()) {
          arg.addAttr(Attribute::ReadNone);
          arg.addAttr(Attribute::NoCapture);
        }
      }
  }
  if (F.getName() == "memcmp") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyAccessesArgMemory();
    F.setOnlyReadsMemory();
#else
    F.addFnAttr(Attribute::ArgMemOnly);
    F.addFnAttr(Attribute::ReadOnly);
#endif
    F.addFnAttr(Attribute::NoUnwind);
    F.addFnAttr(Attribute::NoRecurse);
    F.addFnAttr(Attribute::WillReturn);
    F.addFnAttr(Attribute::NoFree);
    F.addFnAttr(Attribute::NoSync);
    for (int i = 0; i < 2; i++)
      if (F.getFunctionType()->getParamType(i)->isPointerTy()) {
        F.addParamAttr(i, Attribute::NoCapture);
        F.addParamAttr(i, Attribute::WriteOnly);
      }
  }

  if (F.getName() ==
      "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_createERmm") {
    changed = true;
    F.addFnAttr(Attribute::NoFree);
  }
  if (F.getName() == "MPI_Irecv" || F.getName() == "PMPI_Irecv") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyAccessesInaccessibleMemOrArgMem();
#else
    F.addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
#endif
    F.addFnAttr(Attribute::NoUnwind);
    F.addFnAttr(Attribute::NoRecurse);
    F.addFnAttr(Attribute::WillReturn);
    F.addFnAttr(Attribute::NoFree);
    F.addFnAttr(Attribute::NoSync);
    F.addParamAttr(0, Attribute::WriteOnly);
    if (F.getFunctionType()->getParamType(2)->isPointerTy()) {
      F.addParamAttr(2, Attribute::NoCapture);
      F.addParamAttr(2, Attribute::WriteOnly);
    }
    F.addParamAttr(6, Attribute::WriteOnly);
  }
  if (F.getName() == "MPI_Isend" || F.getName() == "PMPI_Isend") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyAccessesInaccessibleMemOrArgMem();
#else
    F.addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
#endif
    F.addFnAttr(Attribute::NoUnwind);
    F.addFnAttr(Attribute::NoRecurse);
    F.addFnAttr(Attribute::WillReturn);
    F.addFnAttr(Attribute::NoFree);
    F.addFnAttr(Attribute::NoSync);
    F.addParamAttr(0, Attribute::ReadOnly);
    if (F.getFunctionType()->getParamType(2)->isPointerTy()) {
      F.addParamAttr(2, Attribute::NoCapture);
      F.addParamAttr(2, Attribute::ReadOnly);
    }
    F.addParamAttr(6, Attribute::WriteOnly);
  }
  if (F.getName() == "MPI_Comm_rank" || F.getName() == "PMPI_Comm_rank" ||
      F.getName() == "MPI_Comm_size" || F.getName() == "PMPI_Comm_size") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyAccessesInaccessibleMemOrArgMem();
#else
    F.addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
#endif
    F.addFnAttr(Attribute::NoUnwind);
    F.addFnAttr(Attribute::NoRecurse);
    F.addFnAttr(Attribute::WillReturn);
    F.addFnAttr(Attribute::NoFree);
    F.addFnAttr(Attribute::NoSync);

    if (F.getFunctionType()->getParamType(0)->isPointerTy()) {
      F.addParamAttr(0, Attribute::NoCapture);
      F.addParamAttr(0, Attribute::ReadOnly);
    }
    if (F.getFunctionType()->getParamType(1)->isPointerTy()) {
      F.addParamAttr(1, Attribute::WriteOnly);
      F.addParamAttr(1, Attribute::NoCapture);
    }
  }
  if (F.getName() == "MPI_Wait" || F.getName() == "PMPI_Wait") {
    changed = true;
    F.addFnAttr(Attribute::NoUnwind);
    F.addFnAttr(Attribute::NoRecurse);
    F.addFnAttr(Attribute::WillReturn);
    F.addFnAttr(Attribute::NoFree);
    F.addFnAttr(Attribute::NoSync);
    F.addParamAttr(0, Attribute::NoCapture);
    F.addParamAttr(1, Attribute::WriteOnly);
    F.addParamAttr(1, Attribute::NoCapture);
  }
  if (F.getName() == "MPI_Waitall" || F.getName() == "PMPI_Waitall") {
    changed = true;
    F.addFnAttr(Attribute::NoUnwind);
    F.addFnAttr(Attribute::NoRecurse);
    F.addFnAttr(Attribute::WillReturn);
    F.addFnAttr(Attribute::NoFree);
    F.addFnAttr(Attribute::NoSync);
    F.addParamAttr(1, Attribute::NoCapture);
    F.addParamAttr(2, Attribute::WriteOnly);
    F.addParamAttr(2, Attribute::NoCapture);
  }
  // Map of MPI function name to the arg index of its type argument
  std::map<std::string, int> MPI_TYPE_ARGS = {
      {"MPI_Send", 2},      {"MPI_Ssend", 2},     {"MPI_Bsend", 2},
      {"MPI_Recv", 2},      {"MPI_Brecv", 2},     {"PMPI_Send", 2},
      {"PMPI_Ssend", 2},    {"PMPI_Bsend", 2},    {"PMPI_Recv", 2},
      {"PMPI_Brecv", 2},

      {"MPI_Isend", 2},     {"MPI_Irecv", 2},     {"PMPI_Isend", 2},
      {"PMPI_Irecv", 2},

      {"MPI_Reduce", 3},    {"PMPI_Reduce", 3},

      {"MPI_Allreduce", 3}, {"PMPI_Allreduce", 3}};
  {
    auto found = MPI_TYPE_ARGS.find(F.getName().str());
    if (found != MPI_TYPE_ARGS.end()) {
      for (auto user : F.users()) {
        if (auto CI = dyn_cast<CallBase>(user))
          if (CI->getCalledFunction() == &F) {
            if (Constant *C =
                    dyn_cast<Constant>(CI->getArgOperand(found->second))) {
              while (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
                C = CE->getOperand(0);
              }
              if (auto GV = dyn_cast<GlobalVariable>(C)) {
                if (GV->getName() == "ompi_mpi_cxx_bool") {
                  changed = true;
                  CI->addAttribute(
                      AttributeList::FunctionIndex,
                      Attribute::get(CI->getContext(), "raptor_inactive"));
                }
              }
            }
          }
      }
    }
  }

  if (F.getName() == "omp_get_max_threads" ||
      F.getName() == "omp_get_thread_num") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyAccessesInaccessibleMemory();
    F.setOnlyReadsMemory();
#else
    F.addFnAttr(Attribute::InaccessibleMemOnly);
    F.addFnAttr(Attribute::ReadOnly);
#endif
  }
  if (F.getName() == "frexp" || F.getName() == "frexpf" ||
      F.getName() == "frexpl") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyAccessesArgMemory();
#else
    F.addFnAttr(Attribute::ArgMemOnly);
#endif
    F.addParamAttr(1, Attribute::WriteOnly);
  }
  if (F.getName() == "__fd_sincos_1" || F.getName() == "__fd_cos_1" ||
      F.getName() == "__mth_i_ipowi") {
    changed = true;
#if LLVM_VERSION_MAJOR >= 16
    F.setOnlyReadsMemory();
    F.setOnlyWritesMemory();
#else
    F.addFnAttr(Attribute::ReadNone);
#endif
  }
  auto name = F.getName();

  const char *NonEscapingFns[] = {
      "julia.ptls_states",
      "julia.get_pgcstack",
      "lgamma_r",
      "memcmp",
      "_ZNSt6chrono3_V212steady_clock3nowEv",
      "_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_"
      "createERmm",
      "_ZNKSt8__detail20_Prime_rehash_policy14_M_need_rehashEmmm",
      "fprintf",
      "fwrite",
      "fputc",
      "strtol",
      "getenv",
      "memchr",
      "cublasSetMathMode",
      "cublasSetStream_v2",
      "cuMemPoolTrimTo",
      "cuDeviceGetMemPool",
      "cuStreamSynchronize",
      "cuStreamDestroy",
      "cuStreamQuery",
      "cuCtxGetCurrent",
      "cuDeviceGet",
      "cuDeviceGetName",
      "cuDriverGetVersion",
      "cudaRuntimeGetVersion",
      "cuDeviceGetCount",
      "cuMemPoolGetAttribute",
      "cuMemGetInfo_v2",
      "cuDeviceGetAttribute",
      "cuDevicePrimaryCtxRetain",
  };
  for (auto fname : NonEscapingFns)
    if (name == fname) {
      changed = true;
      F.addAttribute(
          AttributeList::FunctionIndex,
          Attribute::get(F.getContext(), "raptor_no_escaping_allocation"));
    }
  // changed |= attributeTablegen(F);
  return changed;
}

namespace {

Value *GetFunctionValFromValue(Value *fn) {
  while (!isa<Function>(fn)) {
    if (auto ci = dyn_cast<CastInst>(fn)) {
      fn = ci->getOperand(0);
      continue;
    }
    if (auto ci = dyn_cast<ConstantExpr>(fn)) {
      if (ci->isCast()) {
        fn = ci->getOperand(0);
        continue;
      }
    }
    if (auto ci = dyn_cast<BlockAddress>(fn)) {
      fn = ci->getFunction();
      continue;
    }
    if (auto *GA = dyn_cast<GlobalAlias>(fn)) {
      fn = GA->getAliasee();
      continue;
    }
    if (auto *Call = dyn_cast<CallInst>(fn)) {
      if (auto F = Call->getCalledFunction()) {
        SmallPtrSet<Value *, 1> ret;
        for (auto &BB : *F) {
          if (auto RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
            ret.insert(RI->getReturnValue());
          }
        }
        if (ret.size() == 1) {
          auto val = *ret.begin();
          val = GetFunctionValFromValue(val);
          if (isa<Constant>(val)) {
            fn = val;
            continue;
          }
          if (auto arg = dyn_cast<Argument>(val)) {
            fn = Call->getArgOperand(arg->getArgNo());
            continue;
          }
        }
      }
    }
    // if (auto *Call = dyn_cast<InvokeInst>(fn)) {
    //   if (auto F = Call->getCalledFunction()) {
    //     SmallPtrSet<Value *, 1> ret;
    //     for (auto &BB : *F) {
    //       if (auto RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
    //         ret.insert(RI->getReturnValue());
    //       }
    //     }
    //     if (ret.size() == 1) {
    //       auto val = *ret.begin();
    //       while (isa<LoadInst>(val)) {
    //         auto v2 = simplifyLoad(val);
    //         if (v2) {
    //           val = v2;
    //           continue;
    //         }
    //         break;
    //       }
    //       if (isa<Constant>(val)) {
    //         fn = val;
    //         continue;
    //       }
    //       if (auto arg = dyn_cast<Argument>(val)) {
    //         fn = Call->getArgOperand(arg->getArgNo());
    //         continue;
    //       }
    //     }
    //   }
    // }
    // if (auto S = simplifyLoad(fn)) {
    //   fn = S;
    //   continue;
    // }
    break;
  }

  return fn;
}

Function *GetFunctionFromValue(Value *fn) {
  return dyn_cast<Function>(GetFunctionValFromValue(fn));
}

class RaptorBase {
public:
  RaptorLogic Logic;
  RaptorBase(bool PostOpt)
      : Logic(RaptorPostOpt.getNumOccurrences() ? RaptorPostOpt : PostOpt) {
    // initializeLowerAutodiffIntrinsicPass(*PassRegistry::getPassRegistry());
  }

  Function *parseFunctionParameter(CallInst *CI) {
    Value *fn = CI->getArgOperand(0);

    // determine function to differentiate
    if (CI->hasStructRetAttr()) {
      fn = CI->getArgOperand(1);
    }

    Value *ofn = fn;
    fn = GetFunctionFromValue(fn);

    if (!fn || !isa<Function>(fn)) {
      assert(ofn);
      EmitFailure("NoFunctionToDifferentiate", CI->getDebugLoc(), CI,
                  "failed to find fn to differentiate", *CI, " - found - ",
                  *ofn);
      return nullptr;
    }
    if (cast<Function>(fn)->empty()) {
      EmitFailure("EmptyFunctionToDifferentiate", CI->getDebugLoc(), CI,
                  "failed to find fn to differentiate", *CI, " - found - ",
                  *fn);
      return nullptr;
    }

    return cast<Function>(fn);
  }

  /// Returns the parsed truncation and how many arguments were consumed
  std::pair<FloatTruncation, unsigned>
  parseTruncation(CallInst *CI, TruncateMode Mode, unsigned ArgOffset) {
    unsigned ArgNum = CI->arg_size();
    auto Cfrom = cast<ConstantInt>(CI->getArgOperand(ArgOffset));
    if (!Cfrom)
      EmitFailure("NotConstant", CI->getDebugLoc(), CI,
                  "Expected from argument to be constant");
    FloatRepresentation FRFrom = FloatRepresentation::getIEEE(
        (unsigned)Cfrom->getValue().getZExtValue());
    auto Cty = dyn_cast<ConstantInt>(CI->getArgOperand(ArgOffset + 1));
    if (!Cty)
      EmitFailure("NotConstant", CI->getDebugLoc(), CI,
                  "Expected type argument to be constant");

    if (Cty->getValue().getZExtValue() == FloatRepresentation::IEEE) {
      if (ArgNum != 4)
        EmitFailure("WrongArgNum", CI->getDebugLoc(), CI,
                    "Wrong number of arguments for IEEE type");
      auto Cto = cast<ConstantInt>(CI->getArgOperand(ArgOffset + 2));
      if (!Cto)
        EmitFailure("NotConstant", CI->getDebugLoc(), CI,
                    "Expected IEEE width to be constant");
      auto Constructor = FloatRepresentation::getIEEE;
      if (Mode == TruncMemMode) {
        Constructor = FloatRepresentation::getMPFR;
        EmitWarning("UnsupportedTruncation", *CI,
                    "Mem mode truncation to IEEE not supported, switching to "
                    "equivalent MPFR.");
      }
      FloatRepresentation FRTo =
          Constructor((unsigned)Cto->getValue().getZExtValue());
      return {FloatTruncation(FRFrom, FRTo, Mode), 3};

    } else if (Cty->getValue().getZExtValue() == FloatRepresentation::MPFR) {
      if (ArgNum != 5)
        EmitFailure("WrongArgNum", CI->getDebugLoc(), CI,
                    "Wrong number of arguments for MPFR type");
      auto Ctoe = cast<ConstantInt>(CI->getArgOperand(ArgOffset + 2));
      if (!Ctoe)
        EmitFailure("NotConstant", CI->getDebugLoc(), CI,
                    "Expected MPFR exponent width to be constant");
      auto Ctos = cast<ConstantInt>(CI->getArgOperand(ArgOffset + 3));
      if (!Ctos)
        EmitFailure("NotConstant", CI->getDebugLoc(), CI,
                    "Expected MPFR significand width to be constant");
      return {FloatTruncation(FRFrom,
                              FloatRepresentation::getMPFR(
                                  (unsigned)Ctoe->getValue().getZExtValue(),
                                  (unsigned)Ctos->getValue().getZExtValue()),
                              Mode),
              4};
    }

    EmitFailure("NotConstant", CI->getDebugLoc(), CI, "Unknown float type");
    llvm_unreachable("Unknown float type");
  }

  bool HandleTruncateFunc(CallInst *CI, TruncateMode Mode) {
    IRBuilder<> Builder(CI);
    Function *F = parseFunctionParameter(CI);
    if (!F)
      return false;
    unsigned ArgNum = CI->arg_size();
    if (ArgNum != 4 && ArgNum != 5) {
      EmitFailure("TooManyArgs", CI->getDebugLoc(), CI,
                  "Had incorrect number of args to __raptor_truncate_func", *CI,
                  " - expected 4 or 5");
      return false;
    }
    auto [Truncation, NumArgsParsed] = parseTruncation(CI, Mode, 1);

    RequestContext context(CI, &Builder);
    llvm::Value *res = Logic.CreateTruncateFunc(context, F, Truncation, Mode);
    if (!res)
      return false;
    res = Builder.CreatePointerCast(res, CI->getType());

    CI->replaceAllUsesWith(res);
    CI->eraseFromParent();
    return true;
  }

  bool HandleTruncateValue(CallInst *CI, bool isTruncate) {
    IRBuilder<> Builder(CI);
    unsigned ArgSize = CI->arg_size();
    if (ArgSize != 5 && ArgSize != 4) {
      EmitFailure("TooManyArgs", CI->getDebugLoc(), CI,
                  "Had incorrect number of args to __raptor_truncate_value",
                  *CI, " - expected 3");
      return false;
    }
    RequestContext context(CI, &Builder);
    auto Addr = CI->getArgOperand(0);
    auto [Truncation, NumArgsParsed] = parseTruncation(CI, TruncMemMode, 1);
    return Logic.CreateTruncateValue(context, Addr, Truncation, isTruncate);
  }

  bool handleFlopMemory(Function &F) {
    if (F.isDeclaration())
      return false;
    if (!RaptorTruncateAccessCount)
      return false;

    if (F.getName().starts_with(RaptorFPRTPrefix))
      return false;

    auto M = F.getParent();
    auto &DL = M->getDataLayout();
    IRBuilder<> B(M->getContext());

    auto fname = std::string(RaptorFPRTPrefix) + "memory_access";
    Function *AccessF = M->getFunction(fname);
    Type *PtrTy = PointerType::get(M->getContext(), 0);
    if (!AccessF) {
      FunctionType *FnTy =
          FunctionType::get(Type::getVoidTy(M->getContext()),
                            {PtrTy, Type::getInt64Ty(M->getContext()),
                             Type::getInt64Ty(M->getContext())},
                            /*is_vararg*/ false);
      AccessF = Function::Create(FnTy, Function::ExternalLinkage, fname, M);
    }

    for (auto &BB : F) {
      for (auto &I : BB) {
        uint64_t isStore;
        Type *ty;
        Value *ptr;
        if (auto load = dyn_cast<LoadInst>(&I)) {
          isStore = false;
          ty = load->getType();
          ptr = load->getPointerOperand();
        } else if (auto store = dyn_cast<StoreInst>(&I)) {
          isStore = true;
          ty = store->getValueOperand()->getType();
          ptr = store->getPointerOperand();
        } else {
          continue;
        }
        uint64_t size = DL.getTypeStoreSize(ty);
        CallInst::Create(AccessF,
                         {B.CreateAddrSpaceCast(ptr, PtrTy), B.getInt64(size),
                          B.getInt64(isStore)},
                         "", I.getIterator());
      }
    }

    return true;
  }

  bool handleFlopCount(Function &F) {
    if (F.isDeclaration())
      return false;
    if (!RaptorTruncateCount)
      return false;

    if (F.getName().starts_with(RaptorFPRTPrefix))
      return false;

    for (auto Repr :
         {FloatRepresentation::getIEEE(16), FloatRepresentation::getIEEE(32),
          FloatRepresentation::getIEEE(64)}) {
      IRBuilder<> Builder(F.getContext());
      RequestContext context(&*F.getEntryBlock().begin(), &Builder);
      Function *TruncatedFunc = Logic.CreateTruncateFunc(
          context, &F, FloatTruncation(Repr, TruncCountMode), TruncCountMode);

      ValueToValueMapTy Mapping;
      for (auto &&[Arg, TArg] : llvm::zip(F.args(), TruncatedFunc->args()))
        Mapping[&TArg] = &Arg;

      // Move the truncated body into the original function
      F.deleteBody();
      F.splice(F.begin(), TruncatedFunc);
      RemapFunction(F, Mapping,
                    RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      TruncatedFunc->deleteBody();
    }
    return true;
  }

  bool handleFullModuleTrunc(Function &F) {
    if (startsWith(F.getName(), RaptorFPRTPrefix))
      return false;
    typedef std::vector<FloatTruncation> TruncationsTy;
    static TruncationsTy FullModuleTruncs = []() -> TruncationsTy {
      StringRef ConfigStr(RaptorTruncateAll);
      auto Invalid = [=]() {
        // TODO emit better diagnostic
        llvm::report_fatal_error("error: invalid format for truncation config");
      };

      // Parse "ieee(64)-mpfr(11, 13);ieee(32)-ieee(16)"
      TruncationsTy Tmp;
      while (true) {
        if (ConfigStr.empty())
          break;
        auto From = FloatRepresentation::parse(ConfigStr);
        if (!From)
          Invalid();
        if (ConfigStr.empty())
          Invalid();
        if (!ConfigStr.consume_front("-"))
          Invalid();
        auto To = FloatRepresentation::parse(ConfigStr);
        if (!To)
          Invalid();
        Tmp.push_back({*From, *To, TruncOpFullModuleMode});
        ConfigStr.consume_front(";");
      }
      return Tmp;
    }();

    if (FullModuleTruncs.empty())
      return false;

    // TODO sort truncations (64to32, then 32to16 will make everything 16)
    for (auto Truncation : FullModuleTruncs) {
      IRBuilder<> Builder(F.getContext());
      RequestContext context(&*F.getEntryBlock().begin(), &Builder);
      Function *TruncatedFunc = Logic.CreateTruncateFunc(
          context, &F, Truncation, TruncOpFullModuleMode);

      ValueToValueMapTy Mapping;
      for (auto &&[Arg, TArg] : llvm::zip(F.args(), TruncatedFunc->args()))
        Mapping[&TArg] = &Arg;

      // Move the truncated body into the original function
      F.deleteBody();
      F.splice(F.begin(), TruncatedFunc);
      RemapFunction(F, Mapping,
                    RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      TruncatedFunc->eraseFromParent();
    }
    return true;
  }

  bool lowerRaptorCalls(Function &F, std::set<Function *> &done) {
    if (!RaptorTruncateAll.empty() && RaptorTruncateCount)
      llvm::report_fatal_error(
          "error: trunc all and trunc count are incompatible");

    if (done.count(&F))
      return false;
    done.insert(&F);

    if (F.empty())
      return false;

    if (handleFullModuleTrunc(F))
      return true;

    bool Changed = false;

    for (BasicBlock &BB : F)
      if (InvokeInst *II = dyn_cast<InvokeInst>(BB.getTerminator())) {

        Function *Fn = II->getCalledFunction();

        if (auto castinst = dyn_cast<ConstantExpr>(II->getCalledOperand())) {
          if (castinst->isCast())
            if (auto fn = dyn_cast<Function>(castinst->getOperand(0)))
              Fn = fn;
        }
        if (!Fn)
          continue;

        if (!Fn->getName().contains("__raptor"))
          continue;

        SmallVector<Value *, 16> CallArgs(II->arg_begin(), II->arg_end());
        SmallVector<OperandBundleDef, 1> OpBundles;
        II->getOperandBundlesAsDefs(OpBundles);
        // Insert a normal call instruction...
        CallInst *NewCall =
            CallInst::Create(II->getFunctionType(), II->getCalledOperand(),
                             CallArgs, OpBundles, "", II->getIterator());
        NewCall->takeName(II);
        NewCall->setCallingConv(II->getCallingConv());
        NewCall->setAttributes(II->getAttributes());
        NewCall->setDebugLoc(II->getDebugLoc());
        II->replaceAllUsesWith(NewCall);

        // Insert an unconditional branch to the normal destination.
        BranchInst::Create(II->getNormalDest(), II->getIterator());

        // Remove any PHI node entries from the exception destination.
        II->getUnwindDest()->removePredecessor(&BB);

        II->eraseFromParent();
        Changed = true;
      }

    SmallVector<CallInst *, 4> toTruncateFuncMem;
    SmallVector<CallInst *, 4> toTruncateFuncOp;
    SmallVector<CallInst *, 4> toTruncateValue;
    SmallVector<CallInst *, 4> toExpandValue;
  retry:;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        CallInst *CI = dyn_cast<CallInst>(&I);

        if (!CI)
          continue;

        Function *Fn = nullptr;

        Value *FnOp = CI->getCalledOperand();
        while (true) {
          if ((Fn = dyn_cast<Function>(FnOp)))
            break;
          if (auto castinst = dyn_cast<ConstantExpr>(FnOp)) {
            if (castinst->isCast()) {
              FnOp = castinst->getOperand(0);
              continue;
            }
          }
          break;
        }

        if (!Fn)
          continue;

        size_t num_args = CI->arg_size();

        if (Fn->getName() == "omp_get_max_threads" ||
            Fn->getName() == "omp_get_thread_num") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemory();
          CI->setOnlyAccessesInaccessibleMemory();
          Fn->setOnlyReadsMemory();
          CI->setOnlyReadsMemory();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOnly);
          Fn->addFnAttr(Attribute::ReadOnly);
          CI->addAttribute(AttributeList::FunctionIndex, Attribute::ReadOnly);
#endif
        }
        if ((Fn->getName() == "cblas_ddot" || Fn->getName() == "cblas_sdot") &&
            Fn->isDeclaration()) {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesArgMemory();
          Fn->setOnlyReadsMemory();
          CI->setOnlyReadsMemory();
#else
          Fn->addFnAttr(Attribute::ArgMemOnly);
          Fn->addFnAttr(Attribute::ReadOnly);
          CI->addAttribute(AttributeList::FunctionIndex, Attribute::ReadOnly);
#endif
          CI->addParamAttr(1, Attribute::ReadOnly);
          CI->addParamAttr(1, Attribute::NoCapture);
          CI->addParamAttr(3, Attribute::ReadOnly);
          CI->addParamAttr(3, Attribute::NoCapture);
        }
        if (Fn->getName() == "frexp" || Fn->getName() == "frexpf" ||
            Fn->getName() == "frexpl") {
#if LLVM_VERSION_MAJOR >= 16
          CI->setOnlyAccessesArgMemory();
#else
          CI->addAttribute(AttributeList::FunctionIndex, Attribute::ArgMemOnly);
#endif
          CI->addParamAttr(1, Attribute::WriteOnly);
        }
        if (Fn->getName() == "__fd_sincos_1" || Fn->getName() == "__fd_cos_1" ||
            Fn->getName() == "__mth_i_ipowi") {
#if LLVM_VERSION_MAJOR >= 16
          CI->setOnlyReadsMemory();
          CI->setOnlyWritesMemory();
#else
          CI->addAttribute(AttributeList::FunctionIndex, Attribute::ReadNone);
#endif
        }
        if (Fn->getName().contains("strcmp")) {
          Fn->addParamAttr(0, Attribute::ReadOnly);
          Fn->addParamAttr(1, Attribute::ReadOnly);
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyReadsMemory();
          CI->setOnlyReadsMemory();
#else
          Fn->addFnAttr(Attribute::ReadOnly);
          CI->addAttribute(AttributeList::FunctionIndex, Attribute::ReadOnly);
#endif
        }
        if (Fn->getName() == "f90io_fmtw_end" ||
            Fn->getName() == "f90io_unf_end") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemory();
          CI->setOnlyAccessesInaccessibleMemory();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOnly);
#endif
        }
        if (Fn->getName() == "f90io_open2003a") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemOrArgMem();
          CI->setOnlyAccessesInaccessibleMemOrArgMem();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOrArgMemOnly);
#endif
          for (size_t i : {0, 1, 2, 3, 4, 5, 6, 7, /*8, */ 9, 10, 11, 12, 13}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::ReadOnly);
            }
          }
          // todo more
          for (size_t i : {0, 1}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::NoCapture);
            }
          }
        }
        if (Fn->getName() == "f90io_fmtw_inita") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemOrArgMem();
          CI->setOnlyAccessesInaccessibleMemOrArgMem();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOrArgMemOnly);
#endif
          // todo more
          for (size_t i : {0, 2}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::ReadOnly);
            }
          }

          // todo more
          for (size_t i : {0, 2}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::NoCapture);
            }
          }
        }

        if (Fn->getName() == "f90io_unf_init") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemOrArgMem();
          CI->setOnlyAccessesInaccessibleMemOrArgMem();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOrArgMemOnly);
#endif
          // todo more
          for (size_t i : {0, 1, 2, 3}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::ReadOnly);
            }
          }

          // todo more
          for (size_t i : {0, 1, 2, 3}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::NoCapture);
            }
          }
        }

        if (Fn->getName() == "f90io_src_info03a") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemOrArgMem();
          CI->setOnlyAccessesInaccessibleMemOrArgMem();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOrArgMemOnly);
#endif
          // todo more
          for (size_t i : {0, 1}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::ReadOnly);
            }
          }

          // todo more
          for (size_t i : {0}) {
            if (i < num_args &&
                CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::NoCapture);
            }
          }
        }
        if (Fn->getName() == "f90io_sc_d_fmt_write" ||
            Fn->getName() == "f90io_sc_i_fmt_write" ||
            Fn->getName() == "ftnio_fmt_write64" ||
            Fn->getName() == "f90io_fmt_write64_aa" ||
            Fn->getName() == "f90io_fmt_writea" ||
            Fn->getName() == "f90io_unf_writea" ||
            Fn->getName() == "f90_pausea") {
#if LLVM_VERSION_MAJOR >= 16
          Fn->setOnlyAccessesInaccessibleMemOrArgMem();
          CI->setOnlyAccessesInaccessibleMemOrArgMem();
#else
          Fn->addFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
          CI->addAttribute(AttributeList::FunctionIndex,
                           Attribute::InaccessibleMemOrArgMemOnly);
#endif
          for (size_t i = 0; i < num_args; ++i) {
            if (CI->getArgOperand(i)->getType()->isPointerTy()) {
              CI->addParamAttr(i, Attribute::ReadOnly);
              CI->addParamAttr(i, Attribute::NoCapture);
            }
          }
        }

        bool enableRaptor = false;
        bool truncateFuncOp = false;
        bool truncateFuncMem = false;
        bool truncateValue = false;
        bool expandValue = false;
        if (false) {
        } else if (Fn->getName().contains("__raptor_truncate_mem_func")) {
          enableRaptor = true;
          truncateFuncMem = true;
        } else if (Fn->getName().contains("__raptor_truncate_op_func")) {
          enableRaptor = true;
          truncateFuncOp = true;
        } else if (Fn->getName().contains("__raptor_truncate_mem_value")) {
          enableRaptor = true;
          truncateValue = true;
        } else if (Fn->getName().contains("__raptor_expand_mem_value")) {
          enableRaptor = true;
          expandValue = true;
        }

        if (enableRaptor) {

          Value *fn = CI->getArgOperand(0);
          while (auto ci = dyn_cast<CastInst>(fn)) {
            fn = ci->getOperand(0);
          }
          while (auto ci = dyn_cast<BlockAddress>(fn)) {
            fn = ci->getFunction();
          }
          while (auto ci = dyn_cast<ConstantExpr>(fn)) {
            fn = ci->getOperand(0);
          }
          if (auto si = dyn_cast<SelectInst>(fn)) {
            BasicBlock *post = BB.splitBasicBlock(CI);
            BasicBlock *sel1 = BasicBlock::Create(BB.getContext(), "sel1", &F);
            BasicBlock *sel2 = BasicBlock::Create(BB.getContext(), "sel2", &F);
            BB.getTerminator()->eraseFromParent();
            IRBuilder<> PB(&BB);
            PB.CreateCondBr(si->getCondition(), sel1, sel2);
            IRBuilder<> S1(sel1);
            auto B1 = S1.CreateBr(post);
            CallInst *cloned = cast<CallInst>(CI->clone());
            cloned->insertBefore(B1->getIterator());
            cloned->setOperand(0, si->getTrueValue());
            IRBuilder<> S2(sel2);
            auto B2 = S2.CreateBr(post);
            CI->moveBefore(B2->getIterator());
            CI->setOperand(0, si->getFalseValue());
            if (CI->getNumUses() != 0) {
              IRBuilder<> P(&*post->getFirstNonPHIIt());
              auto merge = P.CreatePHI(CI->getType(), 2);
              merge->addIncoming(cloned, sel1);
              merge->addIncoming(CI, sel2);
              CI->replaceAllUsesWith(merge);
            }
            goto retry;
          }
          if (truncateFuncOp)
            toTruncateFuncOp.push_back(CI);
          else if (truncateFuncMem)
            toTruncateFuncMem.push_back(CI);
          else if (truncateValue)
            toTruncateValue.push_back(CI);
          else if (expandValue)
            toExpandValue.push_back(CI);

          // TODO do we leave this?
          if (auto dc = dyn_cast<Function>(fn)) {
            // Force postopt on any inner functions in the nested
            // AD case.
            bool tmp = Logic.PostOpt;
            Logic.PostOpt = true;
            Changed |= lowerRaptorCalls(*dc, done);
            Logic.PostOpt = tmp;
          }
        }
      }
    }

    for (auto call : toTruncateFuncMem) {
      HandleTruncateFunc(call, TruncMemMode);
    }
    for (auto call : toTruncateFuncOp) {
      HandleTruncateFunc(call, TruncOpMode);
    }
    for (auto call : toTruncateValue) {
      HandleTruncateValue(call, true);
    }
    for (auto call : toExpandValue) {
      HandleTruncateValue(call, false);
    }

    return Changed;
  }

  bool run(Module &M) {
    if (char *Name = getenv("RAPTOR_DUMP_MODULE_PRE")) {
      std::error_code EC;
      raw_fd_stream Out(Name, EC);
      if (!EC) {
        Out << M;
      } else {
        llvm::errs() << "Could not open Raptor dump file.";
      }
    }
    Logic.clear();

    for (Function &F : make_early_inc_range(M)) {
      attributeKnownFunctions(F);
    }

    bool changed = false;
    for (Function &F : M) {
      if (F.empty())
        continue;
      for (BasicBlock &BB : F) {
        for (Instruction &I : make_early_inc_range(BB)) {
          if (auto CI = dyn_cast<CallInst>(&I)) {
            Function *F = CI->getCalledFunction();
            if (auto castinst =
                    dyn_cast<ConstantExpr>(CI->getCalledOperand())) {
              if (castinst->isCast())
                if (auto fn = dyn_cast<Function>(castinst->getOperand(0))) {
                  F = fn;
                }
            }
            if (F && F->getName() == "f90_mzero8") {
              IRBuilder<> B(CI);

              SmallVector<Value *, 4> args;
              args.push_back(CI->getArgOperand(0));
              args.push_back(
                  ConstantInt::get(Type::getInt8Ty(M.getContext()), 0));
              args.push_back(B.CreateMul(
                  CI->getArgOperand(1),
                  ConstantInt::get(CI->getArgOperand(1)->getType(), 8)));
              args.push_back(ConstantInt::getFalse(M.getContext()));

              Type *tys[] = {args[0]->getType(), args[2]->getType()};
              auto memsetIntr =
                  Intrinsic::getOrInsertDeclaration(&M, Intrinsic::memset, tys);
              B.CreateCall(memsetIntr, args);

              CI->eraseFromParent();
            }
          }
        }
      }
    }

    for (Function &F : M) {
      changed |= handleFlopMemory(F);
    }

    std::set<Function *> done;
    for (Function &F : M) {
      if (F.empty())
        continue;

      changed |= lowerRaptorCalls(F, done);
    }

    for (Function &F : M) {
      changed |= handleFlopCount(F);
    }

    Logic.clear();

    if (changed && Logic.PostOpt) {
      TimeTraceScope timeScope("Raptor PostOpt", M.getName());

      PassBuilder PB;
      LoopAnalysisManager LAM;
      FunctionAnalysisManager FAM;
      CGSCCAnalysisManager CGAM;
      ModuleAnalysisManager MAM;
      PB.registerModuleAnalyses(MAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
      auto PM = PB.buildModuleSimplificationPipeline(OptimizationLevel::O2,
                                                     ThinOrFullLTOPhase::None);
      PM.run(M, MAM);
      if (RaptorOMPOpt) {
        OpenMPOptPass().run(M, MAM);
        /// Attributor is run second time for promoted args to get attributes.
        AttributorPass().run(M, MAM);
        for (auto &F : M)
          if (!F.empty())
            PromotePass().run(F, FAM);
      }
    }

    if (char *Name = getenv("RAPTOR_DUMP_MODULE_POST")) {
      std::error_code EC;
      raw_fd_stream Out(Name, EC);
      if (!EC) {
        Out << M;
      } else {
        llvm::errs() << "Could not open Raptor dump file.";
      }
    }
    return changed;
  }
};

class RaptorOldPM : public RaptorBase, public ModulePass {
public:
  static char ID;
  RaptorOldPM(bool PostOpt = false) : RaptorBase(PostOpt), ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();

    // AU.addRequiredID(LCSSAID);

    // LoopInfo is required to ensure that all loops have preheaders
    // AU.addRequired<LoopInfoWrapperPass>();

    // AU.addRequiredID(llvm::LoopSimplifyID);//<LoopSimplifyWrapperPass>();
  }
  bool runOnModule(Module &M) override { return run(M); }
};

} // namespace

char RaptorOldPM::ID = 0;

static RegisterPass<RaptorOldPM> X("raptor", "Raptor Pass");

ModulePass *createRaptorPass(bool PostOpt) { return new RaptorOldPM(PostOpt); }

#include <llvm-c/Core.h>
#include <llvm-c/Types.h>

#include "llvm/IR/LegacyPassManager.h"

extern "C" void AddRaptorPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createRaptorPass(/*PostOpt*/ false));
}

#include "llvm/Passes/PassPlugin.h"

class RaptorNewPM final : public RaptorBase,
                          public AnalysisInfoMixin<RaptorNewPM> {
  friend struct llvm::AnalysisInfoMixin<RaptorNewPM>;

private:
  static llvm::AnalysisKey Key;

public:
  using Result = llvm::PreservedAnalyses;
  RaptorNewPM(bool PostOpt = false) : RaptorBase(PostOpt) {}

  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM) {
    return RaptorBase::run(M) ? PreservedAnalyses::none()
                              : PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
};

#undef DEBUG_TYPE
AnalysisKey RaptorNewPM::Key;

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/GlobalSplit.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/SROA.h"
// #include "llvm/Transforms/IPO/MemProfContextDisambiguation.h"
#include "llvm/Transforms/IPO/ArgumentPromotion.h"
#include "llvm/Transforms/Scalar/ConstraintElimination.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/NewGVN.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#if LLVM_VERSION_MAJOR >= 17
#include "llvm/Transforms/Utils/MoveAutoInit.h"
#endif
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopFlatten.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"

static InlineParams getInlineParamsFromOptLevel(OptimizationLevel Level) {
  return getInlineParams(Level.getSpeedupLevel(), Level.getSizeLevel());
}

#include "llvm/Transforms/Scalar/LowerConstantIntrinsics.h"
#include "llvm/Transforms/Scalar/LowerMatrixIntrinsics.h"
namespace llvm {
extern cl::opt<unsigned> SetLicmMssaNoAccForPromotionCap;
extern cl::opt<unsigned> SetLicmMssaOptCap;
#define EnableLoopFlatten false
#define EagerlyInvalidateAnalyses false
#define RunNewGVN false
#define EnableConstraintElimination true
#define UseInlineAdvisor InliningAdvisorMode::Default
#define EnableMemProfContextDisambiguation false
// extern cl::opt<bool> EnableMatrix;
#define EnableMatrix false
#define EnableModuleInliner false
} // namespace llvm

void augmentPassBuilder(llvm::PassBuilder &PB) {

  auto PB0 = new llvm::PassBuilder(PB);
  auto prePass = [PB0](ModulePassManager &MPM, OptimizationLevel Level) {
    FunctionPassManager OptimizePM;
    OptimizePM.addPass(Float2IntPass());
    OptimizePM.addPass(LowerConstantIntrinsicsPass());

    if (EnableMatrix) {
      OptimizePM.addPass(LowerMatrixIntrinsicsPass());
      OptimizePM.addPass(EarlyCSEPass());
    }

    LoopPassManager LPM;
    bool LTOPreLink = false;
    // First rotate loops that may have been un-rotated by prior passes.
    // Disable header duplication at -Oz.
    LPM.addPass(LoopRotatePass(Level != OptimizationLevel::Oz, LTOPreLink));
    // Some loops may have become dead by now. Try to delete them.
    // FIXME: see discussion in https://reviews.llvm.org/D112851,
    //        this may need to be revisited once we run GVN before
    //        loop deletion in the simplification pipeline.
    LPM.addPass(LoopDeletionPass());

    LPM.addPass(llvm::LoopFullUnrollPass());
    OptimizePM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM)));

    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(OptimizePM)));
  };

  auto loadPass = [prePass](ModulePassManager &MPM, OptimizationLevel Level,
                            ThinOrFullLTOPhase Phase) {
    if (!RaptorEnable)
      return;

    if (Level != OptimizationLevel::O0)
      prePass(MPM, Level);
    MPM.addPass(llvm::AlwaysInlinerPass());
    FunctionPassManager OptimizerPM;
    FunctionPassManager OptimizerPM2;
    OptimizerPM.addPass(llvm::GVNPass());
    OptimizerPM.addPass(llvm::SROAPass(llvm::SROAOptions::PreserveCFG));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(OptimizerPM)));
    MPM.addPass(RaptorNewPM(/*PostOpt=*/true));
    OptimizerPM2.addPass(llvm::GVNPass());
    OptimizerPM2.addPass(llvm::SROAPass(llvm::SROAOptions::PreserveCFG));

    LoopPassManager LPM1;
    LPM1.addPass(LoopDeletionPass());
    OptimizerPM2.addPass(createFunctionToLoopPassAdaptor(std::move(LPM1)));

    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(OptimizerPM2)));
    MPM.addPass(GlobalOptPass());
  };
  // TODO need for perf reasons to move Raptor pass to the pre vectorization.
  PB.registerOptimizerEarlyEPCallback(loadPass);

  auto preLTOPass = [](ModulePassManager &MPM, OptimizationLevel Level) {
    // Create a function that performs CFI checks for cross-DSO calls with
    // targets in the current module.
    MPM.addPass(CrossDSOCFIPass());

    if (Level == OptimizationLevel::O0) {
      return;
    }

    // Try to run OpenMP optimizations, quick no-op if no OpenMP metadata
    // present.
    MPM.addPass(OpenMPOptPass(ThinOrFullLTOPhase::FullLTOPostLink));

    // Remove unused virtual tables to improve the quality of code
    // generated by whole-program devirtualization and bitset lowering.
    MPM.addPass(GlobalDCEPass());

    // Do basic inference of function attributes from known properties of
    // system libraries and other oracles.
    MPM.addPass(InferFunctionAttrsPass());

    if (Level.getSpeedupLevel() > 1) {
      MPM.addPass(createModuleToFunctionPassAdaptor(CallSiteSplittingPass(),
                                                    EagerlyInvalidateAnalyses));

      // Indirect call promotion. This should promote all the targets that
      // are left by the earlier promotion pass that promotes intra-module
      // targets. This two-step promotion is to save the compile time. For
      // LTO, it should produce the same result as if we only do promotion
      // here.
      // MPM.addPass(PGOIndirectCallPromotion(
      //	true /* InLTO */, PGOOpt && PGOOpt->Action ==
      // PGOOptions::SampleUse));

      // Propagate constants at call sites into the functions they call.
      // This opens opportunities for globalopt (and inlining) by
      // substituting function pointers passed as arguments to direct uses
      // of functions.
      MPM.addPass(IPSCCPPass(IPSCCPOptions(/*AllowFuncSpec=*/
                                           Level != OptimizationLevel::Os &&
                                           Level != OptimizationLevel::Oz)));

      // Attach metadata to indirect call sites indicating the set of
      // functions they may target at run-time. This should follow IPSCCP.
      MPM.addPass(CalledValuePropagationPass());
    }

    // Now deduce any function attributes based in the current code.
    MPM.addPass(
        createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

    // Do RPO function attribute inference across the module to
    // forward-propagate attributes where applicable.
    // FIXME: Is this really an optimization rather than a
    // canonicalization?
    MPM.addPass(ReversePostOrderFunctionAttrsPass());

    // Use in-range annotations on GEP indices to split globals where
    // beneficial.
    MPM.addPass(GlobalSplitPass());

    // Run whole program optimization of virtual call when the list of
    // callees is fixed. MPM.addPass(WholeProgramDevirtPass(ExportSummary,
    // nullptr));

    // Stop here at -O1.
    if (Level == OptimizationLevel::O1) {
      return;
    }

    // Optimize globals to try and fold them into constants.
    MPM.addPass(GlobalOptPass());

    // Promote any localized globals to SSA registers.
    MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));

    // Linking modules together can lead to duplicate global constant,
    // only keep one copy of each constant.
    MPM.addPass(ConstantMergePass());

    // Remove unused arguments from functions.
    MPM.addPass(DeadArgumentEliminationPass());

    // Reduce the code after globalopt and ipsccp.  Both can open up
    // significant simplification opportunities, and both can propagate
    // functions through function pointers.  When this happens, we often
    // have to resolve varargs calls, etc, so let instcombine do this.
    FunctionPassManager PeepholeFPM;
    PeepholeFPM.addPass(InstCombinePass());
    if (Level.getSpeedupLevel() > 1)
      PeepholeFPM.addPass(AggressiveInstCombinePass());

    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(PeepholeFPM),
                                                  EagerlyInvalidateAnalyses));

    // Note: historically, the PruneEH pass was run first to deduce
    // nounwind and generally clean up exception handling overhead. It
    // isn't clear this is valuable as the inliner doesn't currently care
    // whether it is inlining an invoke or a call. Run the inliner now.
    if (EnableModuleInliner) {
      MPM.addPass(ModuleInlinerPass(getInlineParamsFromOptLevel(Level),
                                    UseInlineAdvisor,
                                    ThinOrFullLTOPhase::FullLTOPostLink));
    } else {
      MPM.addPass(ModuleInlinerWrapperPass(
          getInlineParamsFromOptLevel(Level),
          /* MandatoryFirst */ true,
          InlineContext{ThinOrFullLTOPhase::FullLTOPostLink,
                        InlinePass::CGSCCInliner}));
    }

    // Perform context disambiguation after inlining, since that would
    // reduce the amount of additional cloning required to distinguish the
    // allocation contexts. if (EnableMemProfContextDisambiguation)
    //	MPM.addPass(MemProfContextDisambiguation());

    // Optimize globals again after we ran the inliner.
    MPM.addPass(GlobalOptPass());

    // Run the OpenMPOpt pass again after global optimizations.
    MPM.addPass(OpenMPOptPass(ThinOrFullLTOPhase::FullLTOPostLink));

    // Garbage collect dead functions.
    MPM.addPass(GlobalDCEPass());

    // If we didn't decide to inline a function, check to see if we can
    // transform it to pass arguments by value instead of by reference.
    MPM.addPass(
        createModuleToPostOrderCGSCCPassAdaptor(ArgumentPromotionPass()));

    FunctionPassManager FPM;
    // The IPO Passes may leave cruft around. Clean up after them.
    FPM.addPass(InstCombinePass());

    if (EnableConstraintElimination)
      FPM.addPass(ConstraintEliminationPass());

    FPM.addPass(JumpThreadingPass());


    // Break up allocas
    FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

    // LTO provides additional opportunities for tailcall elimination due
    // to link-time inlining, and visibility of nocapture attribute.
    FPM.addPass(TailCallElimPass());

    // Run a few AA driver optimizations here and now to cleanup the code.
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM),
                                                  EagerlyInvalidateAnalyses));

    MPM.addPass(
        createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

    // Require the GlobalsAA analysis for the module so we can query it
    // within MainFPM.
    MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());
  };

  auto loadLTO = [preLTOPass, loadPass](ModulePassManager &MPM,
                                        OptimizationLevel Level) {
    preLTOPass(MPM, Level);
    MPM.addPass(
        createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

    // Require the GlobalsAA analysis for the module so we can query it
    // within MainFPM.
    MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());

    // Invalidate AAManager so it can be recreated and pick up the newly
    // available GlobalsAA.
    MPM.addPass(
        createModuleToFunctionPassAdaptor(InvalidateAnalysisPass<AAManager>()));

    FunctionPassManager MainFPM;
    MainFPM.addPass(createFunctionToLoopPassAdaptor(
        LICMPass(SetLicmMssaOptCap, SetLicmMssaNoAccForPromotionCap,
                 /*AllowSpeculation=*/true),
        /*USeMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));

    if (RunNewGVN)
      MainFPM.addPass(NewGVNPass());
    else
      MainFPM.addPass(GVNPass());

    // Remove dead memcpy()'s.
    MainFPM.addPass(MemCpyOptPass());

    // Nuke dead stores.
    MainFPM.addPass(DSEPass());
    MainFPM.addPass(MoveAutoInitPass());
    MainFPM.addPass(MergedLoadStoreMotionPass());

    LoopPassManager LPM;
    if (EnableLoopFlatten && Level.getSpeedupLevel() > 1)
      LPM.addPass(LoopFlattenPass());
    LPM.addPass(IndVarSimplifyPass());
    LPM.addPass(LoopDeletionPass());
    // FIXME: Add loop interchange.

    loadPass(MPM, Level, llvm::ThinOrFullLTOPhase::FullLTOPostLink);
  };
  PB.registerFullLinkTimeOptimizationEarlyEPCallback(loadLTO);
}

void registerRaptor(llvm::PassBuilder &PB) {
#ifdef RAPTOR_AUGMENT_PASS_BUILDER
  augmentPassBuilder(PB);
#endif
  PB.registerPipelineParsingCallback(
      [](llvm::StringRef Name, llvm::ModulePassManager &MPM,
         llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
        if (Name == "raptor") {
          MPM.addPass(RaptorNewPM());
          return true;
        }
        return false;
      });
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Raptor", "v0.1", registerRaptor};
}
