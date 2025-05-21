//===- RaptorLogic.cpp - Implementation of forward and reverse pass generation//
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
#include "RaptorLogic.h"
#include "Utils.h"
#include "llvm-c/Core.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/Instrumentation.h"
#include <cmath>
#include <tuple>

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include <deque>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"

#include "llvm/Demangle/Demangle.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"

#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/TimeProfiler.h"

using namespace llvm;

static Value *floatValTruncate(IRBuilderBase &B, Value *v,
                               FloatTruncation truncation) {
  if (truncation.isToFPRT())
    return v;

  Type *toTy = truncation.getToType(B.getContext());
  if (auto vty = dyn_cast<VectorType>(v->getType()))
    toTy = VectorType::get(toTy, vty->getElementCount());
  return B.CreateFPTrunc(v, toTy, "raptor_trunc");
}

static Value *floatValExpand(IRBuilderBase &B, Value *v,
                             FloatTruncation truncation) {
  if (truncation.isToFPRT())
    return v;

  Type *fromTy = truncation.getFromType(B.getContext());
  if (auto vty = dyn_cast<VectorType>(v->getType()))
    fromTy = VectorType::get(fromTy, vty->getElementCount());
  return B.CreateFPExt(v, fromTy, "raptor_exp");
}

static Value *floatMemTruncate(IRBuilderBase &B, Value *v,
                               FloatTruncation truncation) {
  if (isa<VectorType>(v->getType()))
    report_fatal_error("vector operations not allowed in mem trunc mode");

  Type *toTy = truncation.getToType(B.getContext());
  return B.CreateBitCast(v, toTy);
}

static Value *floatMemExpand(IRBuilderBase &B, Value *v,
                             FloatTruncation truncation) {
  if (isa<VectorType>(v->getType()))
    report_fatal_error("vector operations not allowed in mem trunc mode");

  Type *fromTy = truncation.getFromType(B.getContext());
  return B.CreateBitCast(v, fromTy);
}

class TruncateUtils {
protected:
  FloatTruncation truncation;
  llvm::Module *M;
  Type *fromType;
  Type *toType;
  LLVMContext &ctx;
  RaptorLogic &Logic;
  Value *UnknownLoc;
  Value *scratch = nullptr;

private:
  std::string getOriginalFPRTName(std::string Name) {
    return std::string(RaptorFPRTOriginalPrefix) + truncation.mangleFrom() +
           "_" + Name;
  }
  std::string getFPRTName(std::string Name) {
    return std::string(RaptorFPRTPrefix) + truncation.mangleFrom() + "_" + Name;
  }

  // Creates a function which contains the original floating point operation.
  // The user can use this to compare results against.
  void createOriginalFPRTFunc(Instruction &I, std::string Name,
                              SmallVectorImpl<Value *> &Args,
                              llvm::Type *RetTy) {
    auto MangledName = getOriginalFPRTName(Name);
    auto F = M->getFunction(MangledName);
    if (!F) {
      SmallVector<Type *, 4> ArgTypes;
      for (auto Arg : Args)
        ArgTypes.push_back(Arg->getType());
      FunctionType *FnTy =
          FunctionType::get(RetTy, ArgTypes, /*is_vararg*/ false);
      F = Function::Create(FnTy, Function::WeakAnyLinkage, MangledName, M);
    }
    if (F->isDeclaration()) {
      BasicBlock *Entry = BasicBlock::Create(F->getContext(), "entry", F);
      auto ClonedI = I.clone();
      for (unsigned It = 0; It < Args.size(); It++)
        ClonedI->setOperand(It, F->getArg(It));
      auto Return = ReturnInst::Create(F->getContext(), ClonedI, Entry);
      ClonedI->insertBefore(Return->getIterator());
      F->setLinkage(GlobalValue::WeakODRLinkage);
      // Clear invalidated debug metadata now that we defined the function
      F->clearMetadata();
    }
  }

  Function *getFPRTFunc(std::string Name, SmallVectorImpl<Value *> &Args,
                        llvm::Type *RetTy) {
    auto MangledName = getFPRTName(Name);
    auto F = M->getFunction(MangledName);
    if (!F) {
      SmallVector<Type *, 4> ArgTypes;
      for (auto Arg : Args)
        ArgTypes.push_back(Arg->getType());
      FunctionType *FnTy =
          FunctionType::get(RetTy, ArgTypes, /*is_vararg*/ false);
      F = Function::Create(FnTy, Function::ExternalLinkage, MangledName, M);
    }
    return F;
  }

public:
  CallInst *createFPRTGeneric(llvm::IRBuilderBase &B, std::string Name,
                              const SmallVectorImpl<Value *> &ArgsIn,
                              llvm::Type *RetTy, Value *LocStr) {
    SmallVector<Value *, 5> Args(ArgsIn.begin(), ArgsIn.end());
    Args.push_back(B.getInt64(truncation.getTo().exponentWidth));
    Args.push_back(B.getInt64(truncation.getTo().significandWidth));
    Args.push_back(B.getInt64(truncation.getMode()));
#if LLVM_VERSION_MAJOR <= 14
    Args.push_back(B.CreateBitCast(LocStr, NullPtr->getType()));
#else
    Args.push_back(LocStr);
#endif
    Args.push_back(scratch);

    auto FprtFunc = getFPRTFunc(Name, Args, RetTy);
    // Explicitly assign a dbg location if it didn't exist, as the FPRT
    // functions are inlineable and the backend fails if the callsite does not
    // have dbg metadata
    // TODO consider using InstrumentationIRBuilder
    Function *ContainingF = B.GetInsertBlock()->getParent();
    if (!B.getCurrentDebugLocation() && ContainingF->getSubprogram())
      B.SetCurrentDebugLocation(DILocation::get(ContainingF->getContext(), 0, 0,
                                                ContainingF->getSubprogram()));
    auto *CI = cast<CallInst>(B.CreateCall(FprtFunc, Args));

    return CI;
  }

  TruncateUtils(FloatTruncation truncation, Module *M, RaptorLogic &Logic)
      : truncation(truncation), M(M), ctx(M->getContext()), Logic(Logic) {
    fromType = truncation.getFromType(ctx);
    toType = truncation.getToType(ctx);
    if (fromType == toType)
      assert(truncation.isToFPRT());

    UnknownLoc = getUniquedLocStr(nullptr);
    scratch = ConstantPointerNull::get(PointerType::get(M->getContext(), 0));
  }

  Type *getFromType() { return fromType; }

  Type *getToType() { return toType; }

  CallInst *createFPRTConstCall(llvm::IRBuilderBase &B, Value *V) {
    assert(V->getType() == getFromType());
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    return createFPRTGeneric(B, "const", Args, getToType(), UnknownLoc);
  }
  CallInst *createFPRTNewCall(llvm::IRBuilderBase &B, Value *V) {
    assert(V->getType() == getFromType());
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    return createFPRTGeneric(B, "new", Args, getToType(), UnknownLoc);
  }
  CallInst *createFPRTGetCall(llvm::IRBuilderBase &B, Value *V) {
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    return createFPRTGeneric(B, "get", Args, getToType(), UnknownLoc);
  }
  CallInst *createFPRTDeleteCall(llvm::IRBuilderBase &B, Value *V) {
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    return createFPRTGeneric(B, "delete", Args, B.getVoidTy(), UnknownLoc);
  }
  // This will result in a unique string for each location, which means the
  // runtime can check whether two operations are the same with a simple pointer
  // comparison. However, we need LTO for this to be the case across different
  // compilation units.
  GlobalValue *getUniquedLocStr(Instruction *I) {
    std::string FileName = "unknown";
    unsigned LineNo = 0;
    unsigned ColNo = 0;

    if (I) {
      DILocation *DL = I->getDebugLoc();
      if (DL) {
        FileName = DL->getFilename();
        LineNo = DL->getLine();
        ColNo = DL->getColumn();
      }
    }

    auto Key = std::make_tuple(FileName, LineNo, ColNo);
    auto It = Logic.UniqDebugLocStrs.find(Key);

    if (It != Logic.UniqDebugLocStrs.end())
      return It->second;

    std::string LocStr =
        FileName + ":" + std::to_string(LineNo) + ":" + std::to_string(ColNo);
    auto GV = createPrivateGlobalForString(*M, LocStr, true);
    Logic.UniqDebugLocStrs[Key] = GV;

    return GV;
  }
  CallInst *createFPRTOpCall(llvm::IRBuilderBase &B, llvm::Instruction &I,
                             llvm::Type *RetTy,
                             SmallVectorImpl<Value *> &ArgsIn) {
    if (truncation.getMode() == TruncCountMode) {
      SmallVector<Value *> EmptyArgs;
      return createFPRTGeneric(B, "count", EmptyArgs, B.getVoidTy(),
                               getUniquedLocStr(&I));
    }
    std::string Name;
    if (auto BO = dyn_cast<BinaryOperator>(&I)) {
      Name = "binop_" + std::string(BO->getOpcodeName());
    } else if (auto II = dyn_cast<IntrinsicInst>(&I)) {
      auto FOp = II->getCalledFunction();
      assert(FOp);
      Name = "intr_" + std::string(FOp->getName());
      for (auto &C : Name)
        if (C == '.')
          C = '_';
    } else if (auto CI = dyn_cast<CallInst>(&I)) {
      if (auto F = CI->getCalledFunction())
        Name = "func_" + std::string(F->getName());
      else
        llvm_unreachable(
            "Unexpected indirect call inst for conversion to FPRT");
    } else if (auto CI = dyn_cast<FCmpInst>(&I)) {
      Name = "fcmp_" + std::string(CI->getPredicateName(CI->getPredicate()));
    } else if (auto UO = dyn_cast<UnaryOperator>(&I)) {
      Name = "unaryop_" + std::string(UO->getOpcodeName());
    } else {
      llvm_unreachable("Unexpected instruction for conversion to FPRT");
    }
    createOriginalFPRTFunc(I, Name, ArgsIn, RetTy);
    return createFPRTGeneric(B, Name, ArgsIn, RetTy, getUniquedLocStr(&I));
  }
};

// TODO we need to handle cases where constant aggregates are used and they
// contain constant fp's in them.
//
// e.g. store {0 : i64, 1.0: f64} %ptr
//
// Currently in mem mode the float will remain unconverted and we will likely
// crash somewhere.
class TruncateGenerator : public llvm::InstVisitor<TruncateGenerator>,
                          public TruncateUtils {
private:
  ValueToValueMapTy &originalToNewFn;
  FloatTruncation truncation;
  TruncateMode mode;
  RaptorLogic &Logic;
  LLVMContext &ctx;

public:
  TruncateGenerator(ValueToValueMapTy &originalToNewFn,
                    FloatTruncation truncation, Function *oldFunc,
                    Function *newFunc, RaptorLogic &Logic, bool root)
      : TruncateUtils(truncation, newFunc->getParent(), Logic),
        originalToNewFn(originalToNewFn), truncation(truncation),
        mode(truncation.getMode()), Logic(Logic), ctx(newFunc->getContext()) {

    auto allocScratch = [&]() {
      // TODO we should check at the end if we never used the scracth we should
      // remove the runtime calls for allocation.
      auto getName = "get_scratch";
      auto freeName = "free_scratch";
      IRBuilder<> B(newFunc->getContext());
      B.SetInsertPointPastAllocas(newFunc);
      SmallVector<Value *> args;
      scratch = createFPRTGeneric(B, getName, args, B.getPtrTy(),
                                  getUniquedLocStr(nullptr));
      for (auto &BB : *newFunc) {
        if (ReturnInst *ret = dyn_cast<ReturnInst>(BB.getTerminator())) {
          B.SetInsertPoint(ret);
          createFPRTGeneric(B, freeName, args, B.getPtrTy(),
                            getUniquedLocStr(nullptr));
        }
      }
    };
    if (mode == TruncOpMode) {
      if (root) {
        allocScratch();
      } else {
        assert(newFunc->arg_size() == oldFunc->arg_size() + 1);
        scratch = newFunc->getArg(newFunc->arg_size() - 1);
        assert(scratch->getType()->isPointerTy());
      }
    } else if (mode == TruncOpFullModuleMode) {
      allocScratch();
    }
  }

  void todo(llvm::Instruction &I) {
    if (all_of(I.operands(),
               [&](Use &U) { return U.get()->getType() != fromType; }) &&
        I.getType() != fromType)
      return;

    switch (mode) {
    case TruncMemMode:
      llvm::errs() << I << "\n";
      EmitFailure("FPEscaping", I.getDebugLoc(), &I, "FP value escapes!");
      break;
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      EmitWarning(
          "UnhandledTrunc", I,
          "Operation not handled - it will be executed in the original way.",
          I);
      break;
    default:
      llvm_unreachable("Unknown trunc mode");
    }
  }

  void visitInstruction(llvm::Instruction &I) {
    using namespace llvm;

    switch (I.getOpcode()) {
      // #include "InstructionDerivatives.inc"
    default:
      break;
    }

    todo(I);
  }

  Value *truncate(IRBuilder<> &B, Value *v) {
    switch (mode) {
    case TruncMemMode:
      if (isa<ConstantFP>(v))
        return createFPRTConstCall(B, v);
      return floatMemTruncate(B, v, truncation);
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return floatValTruncate(B, v, truncation);
    case TruncCountMode:
      return nullptr;
    }
    llvm_unreachable("Unknown trunc mode");
  }

  Value *expand(IRBuilder<> &B, Value *v) {
    switch (mode) {
    case TruncMemMode:
      return floatMemExpand(B, v, truncation);
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return floatValExpand(B, v, truncation);
    case TruncCountMode:
      return nullptr;
    }
    llvm_unreachable("Unknown trunc mode");
  }

  void visitUnaryOperator(UnaryOperator &I) {
    switch (I.getOpcode()) {
    case UnaryOperator::FNeg: {
      if (I.getOperand(0)->getType() != getFromType())
        return;

      auto newI = getNewFromOriginal(&I);
      IRBuilder<> B(newI);
      SmallVector<Value *, 2> Args = {newI->getOperand(0)};
      auto nres = createFPRTOpCall(B, I, newI->getType(), Args);
      if (mode != TruncCountMode) {
        nres->takeName(newI);
        nres->copyIRFlags(newI);
        newI->replaceAllUsesWith(nres);
        newI->eraseFromParent();
      }
      return;
    }
    default:
      todo(I);
      return;
    }
  }

  void visitAllocaInst(llvm::AllocaInst &I) { return; }
  void visitICmpInst(llvm::ICmpInst &I) { return; }
  void visitFCmpInst(llvm::FCmpInst &CI) {
    switch (mode) {
    case TruncMemMode: {
      auto LHS = getNewFromOriginal(CI.getOperand(0));
      auto RHS = getNewFromOriginal(CI.getOperand(1));
      if (LHS->getType() != getFromType())
        return;

      auto newI = getNewFromOriginal(&CI);
      IRBuilder<> B(newI);
      auto truncLHS = truncate(B, LHS);
      auto truncRHS = truncate(B, RHS);

      SmallVector<Value *, 2> Args;
      Args.push_back(truncLHS);
      Args.push_back(truncRHS);
      Instruction *nres;
      if (truncation.isToFPRT())
        nres = createFPRTOpCall(B, CI, B.getInt1Ty(), Args);
      else
        nres =
            cast<FCmpInst>(B.CreateFCmp(CI.getPredicate(), truncLHS, truncRHS));
      if (mode != TruncCountMode) {
        nres->takeName(newI);
        nres->copyIRFlags(newI);
        newI->replaceAllUsesWith(nres);
        newI->eraseFromParent();
      }
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      return;
    }
  }
  void visitLoadInst(llvm::LoadInst &LI) {
    auto alignment = LI.getAlign();
    visitLoadLike(LI, alignment);
  }
  void visitStoreInst(llvm::StoreInst &SI) {
    auto align = SI.getAlign();
    visitCommonStore(SI, SI.getPointerOperand(), SI.getValueOperand(), align,
                     SI.isVolatile(), SI.getOrdering(), SI.getSyncScopeID(),
                     /*mask=*/nullptr);
  }
  // TODO Is there a possibility we GEP a const and get a FP value?
  void visitGetElementPtrInst(llvm::GetElementPtrInst &gep) { return; }
  void visitCastInst(llvm::CastInst &CI) {
    // TODO Try to follow fps through trunc/exts
    switch (mode) {
    case TruncMemMode: {
      auto newI = getNewFromOriginal(&CI);
      auto newSrc = newI->getOperand(0);
      if (CI.getSrcTy() == getFromType()) {
        IRBuilder<> B(newI);
        if (isa<Constant>(newSrc))
          return;
        newI->setOperand(0, createFPRTGetCall(B, newSrc));
        EmitWarning("FPNoFollow", CI, "Will not follow FP through this cast.",
                    CI);
      } else if (CI.getDestTy() == getFromType()) {
        IRBuilder<> B(newI->getNextNode());
        EmitWarning("FPNoFollow", CI, "Will not follow FP through this cast.",
                    CI);
        auto nres = createFPRTNewCall(B, newI);
        if (mode != TruncCountMode) {
          nres->takeName(newI);
          nres->copyIRFlags(newI);
          newI->replaceUsesWithIf(nres,
                                  [&](Use &U) { return U.getUser() != nres; });
          originalToNewFn[const_cast<const Value *>(cast<Value>(&CI))] = nres;
        }
      }
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      return;
    }
  }
  void visitSelectInst(llvm::SelectInst &SI) {
    switch (mode) {
    case TruncMemMode: {
      if (SI.getType() != getFromType())
        return;
      auto newI = getNewFromOriginal(&SI);
      IRBuilder<> B(newI);
      auto newT = truncate(B, getNewFromOriginal(SI.getTrueValue()));
      auto newF = truncate(B, getNewFromOriginal(SI.getFalseValue()));
      auto nres = cast<SelectInst>(
          B.CreateSelect(getNewFromOriginal(SI.getCondition()), newT, newF));
      if (mode != TruncCountMode) {
        nres->takeName(newI);
        nres->copyIRFlags(newI);
        newI->replaceAllUsesWith(expand(B, nres));
        newI->eraseFromParent();
      }
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      return;
    }
    llvm_unreachable("");
  }
  void visitExtractElementInst(llvm::ExtractElementInst &EEI) { return; }
  void visitInsertElementInst(llvm::InsertElementInst &EEI) { return; }
  void visitShuffleVectorInst(llvm::ShuffleVectorInst &EEI) { return; }
  void visitExtractValueInst(llvm::ExtractValueInst &EEI) { return; }
  void visitInsertValueInst(llvm::InsertValueInst &EEI) { return; }
  void visitBinaryOperator(llvm::BinaryOperator &BO) {
    auto oldLHS = BO.getOperand(0);
    auto oldRHS = BO.getOperand(1);

    if (oldLHS->getType() != getFromType() &&
        oldRHS->getType() != getFromType())
      return;

    switch (BO.getOpcode()) {
    default:
      break;
    case BinaryOperator::Add:
    case BinaryOperator::Sub:
    case BinaryOperator::Mul:
    case BinaryOperator::UDiv:
    case BinaryOperator::SDiv:
    case BinaryOperator::URem:
    case BinaryOperator::SRem:
    case BinaryOperator::AShr:
    case BinaryOperator::LShr:
    case BinaryOperator::Shl:
    case BinaryOperator::And:
    case BinaryOperator::Or:
    case BinaryOperator::Xor:
      assert(0 && "Invalid binop opcode for float arg");
      return;
    }

    auto newI = getNewFromOriginal(&BO);
    IRBuilder<> B(newI);
    auto newLHS = truncate(B, getNewFromOriginal(oldLHS));
    auto newRHS = truncate(B, getNewFromOriginal(oldRHS));
    Instruction *nres = nullptr;
    if (truncation.isToFPRT()) {
      SmallVector<Value *, 2> Args({newLHS, newRHS});
      nres = createFPRTOpCall(B, BO, truncation.getToType(ctx), Args);
    } else {
      nres = cast<Instruction>(B.CreateBinOp(BO.getOpcode(), newLHS, newRHS));
    }
    if (mode != TruncCountMode) {
      nres->takeName(newI);
      nres->copyIRFlags(newI);
      newI->replaceAllUsesWith(expand(B, nres));
      newI->eraseFromParent();
    }
    return;
  }
  void visitMemSetInst(llvm::MemSetInst &MS) { visitMemSetCommon(MS); }
  void visitMemSetCommon(llvm::CallInst &MS) { return; }
  void visitMemTransferInst(llvm::MemTransferInst &MTI) {
    using namespace llvm;
    Value *isVolatile = getNewFromOriginal(MTI.getOperand(3));
    auto srcAlign = MTI.getSourceAlign();
    auto dstAlign = MTI.getDestAlign();
    visitMemTransferCommon(MTI.getIntrinsicID(), srcAlign, dstAlign, MTI,
                           MTI.getOperand(0), MTI.getOperand(1),
                           getNewFromOriginal(MTI.getOperand(2)), isVolatile);
  }
  void visitMemTransferCommon(llvm::Intrinsic::ID ID, llvm::MaybeAlign srcAlign,
                              llvm::MaybeAlign dstAlign, llvm::CallInst &MTI,
                              llvm::Value *orig_dst, llvm::Value *orig_src,
                              llvm::Value *new_size, llvm::Value *isVolatile) {
    return;
  }
  void visitFenceInst(llvm::FenceInst &FI) { return; }

  bool handleIntrinsic(llvm::CallBase &CI, Intrinsic::ID ID) {
    if (isDbgInfoIntrinsic(ID))
      return true;

    auto newI = cast<llvm::CallBase>(getNewFromOriginal(&CI));
    IRBuilder<> B(newI);

    SmallVector<Value *, 2> orig_ops(CI.arg_size());
    for (unsigned i = 0; i < CI.arg_size(); ++i)
      orig_ops[i] = CI.getOperand(i);

    bool hasFromType = false;
    SmallVector<Value *, 2> new_ops(CI.arg_size());
    for (unsigned i = 0; i < CI.arg_size(); ++i) {
      if (orig_ops[i]->getType() == getFromType()) {
        new_ops[i] = truncate(B, getNewFromOriginal(orig_ops[i]));
        hasFromType = true;
      } else {
        new_ops[i] = getNewFromOriginal(orig_ops[i]);
      }
    }
    Type *retTy = CI.getType();
    if (CI.getType() == getFromType()) {
      hasFromType = true;
      retTy = getToType();
    }

    if (!hasFromType)
      return false;

    Instruction *intr = nullptr;
    Value *nres = nullptr;
    if (truncation.isToFPRT()) {
      nres = intr = createFPRTOpCall(B, CI, retTy, new_ops);
    } else {
      // TODO check that the intrinsic is overloaded
      nres = intr =
          createIntrinsicCall(B, ID, retTy, new_ops, &CI, CI.getName());
    }
    if (newI->getType() == getFromType())
      nres = expand(B, nres);
    if (mode != TruncCountMode) {
      intr->copyIRFlags(newI);
      newI->replaceAllUsesWith(nres);
      newI->eraseFromParent();
    }
    return true;
  }

  void visitIntrinsicInst(llvm::IntrinsicInst &II) {
    handleIntrinsic(II, II.getIntrinsicID());
  }

  void visitReturnInst(llvm::ReturnInst &I) {
    switch (mode) {
    case TruncMemMode: {
      if (I.getNumOperands() == 0)
        return;
      if (I.getReturnValue()->getType() != getFromType())
        return;
      auto newI = cast<llvm::ReturnInst>(getNewFromOriginal(&I));
      IRBuilder<> B(newI);
      if (isa<ConstantFP>(newI->getOperand(0)))
        newI->setOperand(0, createFPRTConstCall(B, newI->getReturnValue()));
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      break;
    default:
      llvm_unreachable("Unknown trunc mode");
    }
  }

  void visitBranchInst(llvm::BranchInst &I) { return; }
  void visitSwitchInst(llvm::SwitchInst &I) { return; }
  void visitUnreachableInst(llvm::UnreachableInst &I) { return; }
  void visitLoadLike(llvm::Instruction &I, llvm::MaybeAlign alignment,
                     llvm::Value *mask = nullptr,
                     llvm::Value *orig_maskInit = nullptr) {
    return;
  }

  void visitCommonStore(llvm::Instruction &I, llvm::Value *orig_ptr,
                        llvm::Value *orig_val, llvm::MaybeAlign prevalign,
                        bool isVolatile, llvm::AtomicOrdering ordering,
                        llvm::SyncScope::ID syncScope, llvm::Value *mask) {
    switch (mode) {
    case TruncMemMode: {
      if (orig_val->getType() != getFromType())
        return;
      if (!isa<ConstantFP>(orig_val))
        return;
      auto newI = getNewFromOriginal(&I);
      IRBuilder<> B(newI);
      newI->setOperand(0, createFPRTConstCall(B, getNewFromOriginal(orig_val)));
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      break;
    default:
      llvm_unreachable("Unknown trunc mode");
    }
    return;
  }

  llvm::Value *getNewFromOriginal(llvm::Value *v) {
    auto found = originalToNewFn.find(v);
    assert(found != originalToNewFn.end());
    return found->second;
  }

  llvm::Instruction *getNewFromOriginal(llvm::Instruction *v) {
    return cast<Instruction>(getNewFromOriginal((llvm::Value *)v));
  }

  bool handleKnownCalls(llvm::CallBase &call, llvm::Function *called,
                        llvm::StringRef funcName,
                        llvm::CallBase *const newCall) {
    return false;
  }

  Value *GetShadow(RequestContext &ctx, Value *v, bool root) {
    if (auto F = dyn_cast<Function>(v))
      return Logic.CreateTruncateFunc(ctx, F, truncation, mode, root);
    llvm::errs() << " unknown get truncated func: " << *v << "\n";
    llvm_unreachable("unknown get truncated func");
    return v;
  }
  // void visitInvokeInst(llvm::InvokeInst &CI) {
  //   // fprintf(stderr, "Won't handle invoke instruction.\n");
  //   EmitWarning("FPNoInvoke", CI,
  //               "Will not handle invoke instruction.", CI);    
  // }
  
  // Return
  void visitCallBase(llvm::CallBase &CI) {
    Intrinsic::ID ID;
    StringRef funcName = getFuncNameFromCall(const_cast<CallBase *>(&CI));
    if (isMemFreeLibMFunction(funcName, &ID))
      if (handleIntrinsic(CI, ID))
        return;

    using namespace llvm;

    CallBase *const newCall = cast<CallBase>(getNewFromOriginal(&CI));
    IRBuilder<> BuilderZ(newCall);

    if (auto called = CI.getCalledFunction())
      if (handleKnownCalls(CI, called, getFuncNameFromCall(&CI), newCall))
        return;

    // if (!newCall->getDebugLoc()) {
    //   Function *ContainingF = newCall->getFunction();
    //   newCall->setDebugLoc(DILocation::get(ContainingF->getContext(), 0, 0,
    //                                        ContainingF->getSubprogram()));
    // }

    if (mode == TruncOpMode || mode == TruncMemMode) {
      RequestContext ctx(&CI, &BuilderZ);
      Function *Func = CI.getCalledFunction();
      if (Func && !Func->empty()) {
        bool truncOpIgnore = Func->getName().contains("raptor_trunc_op_ignore");
        bool truncMemIgnore =
            Func->getName().contains("raptor_trunc_mem_ignore");
        bool truncIgnore = Func->getName().contains("raptor_trunc_ignore");
        truncIgnore |= truncOpIgnore && mode == TruncOpMode;
        truncIgnore |= truncMemIgnore && mode == TruncMemMode;
        if (!truncIgnore) {
          if (scratch && mode == TruncOpMode && isa<CallInst>(&CI)) {
            auto val = GetShadow(ctx, getNewFromOriginal(CI.getCalledOperand()),
                                 false);
            Function *F = cast<Function>(val);
            IRBuilder<> B(newCall);
            SmallVector<Value *> args(newCall->args());
            args.push_back(scratch);
            CallInst *newNewCall = B.CreateCall(F, args);
            newNewCall->copyMetadata(*newCall);
            newNewCall->copyIRFlags(newCall);
            newNewCall->setAttributes(newCall->getAttributes());
            newNewCall->setCallingConv(newCall->getCallingConv());
            // newNewCall->setTailCallKind(newCall->getTailCallKind());
            newNewCall->setDebugLoc(newCall->getDebugLoc());
            newCall->replaceAllUsesWith(newNewCall);
            newCall->eraseFromParent();
            // TODO not sure if we need to change the originalToNewFn mapping.
          } else {
            auto val =
                GetShadow(ctx, getNewFromOriginal(CI.getCalledOperand()), true);
            newCall->setCalledOperand(val);
          }
        }
      } else if (!Func) {
        switch (mode) {
        case TruncMemMode:
        case TruncOpMode:
          // fprintf(stderr, "Won't follow indirect call.\n");
          EmitWarning("FPNoFollow", CI,
                      "Will not follow FP through this indirect call.", CI);
          break;
        default:
          llvm_unreachable("Unknown trunc mode");
        }
      } else {
        switch (mode) {
        case TruncMemMode:
          EmitWarning("FPNoFollow", CI,
                      "Will not follow FP through this function call as the "
                      "definition is not available.",
                      CI);
          break;
        case TruncOpMode:
          EmitWarning("FPNoFollow", CI,
                      "Will not truncate flops in this function call as the "
                      "definition is not available.",
                      CI);
          break;
        default:
          llvm_unreachable("Unknown trunc mode");
        }
      }
    }
    return;
  }
  void visitPHINode(llvm::PHINode &PN) {
    switch (mode) {
    case TruncMemMode: {
      if (PN.getType() != getFromType())
        return;
      auto NewPN = cast<llvm::PHINode>(getNewFromOriginal(&PN));
      IRBuilder<> B(
          &*NewPN->getParent()->getParent()->getEntryBlock().getFirstNonPHIIt());
      for (unsigned It = 0; It < NewPN->getNumIncomingValues(); It++) {
        if (isa<ConstantFP>(NewPN->getIncomingValue(It))) {
          NewPN->setOperand(
              It, createFPRTConstCall(B, NewPN->getIncomingValue(It)));
        }
      }
      break;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
    case TruncCountMode:
      break;
    default:
      llvm_unreachable("Unknown trunc mode");
    }
  }
};

bool RaptorLogic::CreateTruncateValue(RequestContext context, Value *v,
                                      FloatRepresentation from,
                                      FloatRepresentation to, bool isTruncate) {
  assert(context.req && context.ip);

  IRBuilderBase &B = *context.ip;

  Value *converted = nullptr;
  auto truncation = FloatTruncation(from, to, TruncMemMode);
  TruncateUtils TU(truncation, B.GetInsertBlock()->getParent()->getParent(),
                   *this);
  if (isTruncate)
    converted = TU.createFPRTNewCall(B, v);
  else
    converted = TU.createFPRTGetCall(B, v);
  assert(converted);

  context.req->replaceAllUsesWith(converted);
  context.req->eraseFromParent();

  return true;
}

llvm::Function *RaptorLogic::CreateTruncateFunc(RequestContext context,
                                                llvm::Function *totrunc,
                                                FloatTruncation truncation,
                                                TruncateMode mode, bool root) {
  TruncateCacheKey tup(totrunc, truncation, mode, root);
  if (TruncateCachedFunctions.find(tup) != TruncateCachedFunctions.end()) {
    return TruncateCachedFunctions.find(tup)->second;
  }

  IRBuilder<> B(totrunc->getContext());

  FunctionType *orig_FTy = totrunc->getFunctionType();
  SmallVector<Type *, 4> params;

  for (unsigned i = 0; i < orig_FTy->getNumParams(); ++i) {
    params.push_back(orig_FTy->getParamType(i));
  }

  if (mode == TruncOpMode && !root)
    params.push_back(B.getPtrTy());

  Type *NewTy = totrunc->getReturnType();

  FunctionType *FTy = FunctionType::get(NewTy, params, totrunc->isVarArg());
  std::string truncName =
      std::string("__raptor_done_truncate_") + truncateModeStr(mode) +
      "_func_" + truncation.mangleTruncation() + "_" + totrunc->getName().str();
  Function *NewF = Function::Create(FTy, totrunc->getLinkage(), truncName,
                                    totrunc->getParent());

  if (mode != TruncOpFullModuleMode && mode != TruncCountMode)
    NewF->setLinkage(Function::LinkageTypes::InternalLinkage);

  TruncateCachedFunctions[tup] = NewF;

  if (totrunc->empty()) {
    std::string s;
    llvm::raw_string_ostream ss(s);
    ss << "No truncate mode found for " + totrunc->getName() << "\n";
    // llvm::Value *toshow = totrunc;
    if (context.req) {
      // toshow = context.req;
      ss << " at context: " << *context.req;
    } else {
      ss << *totrunc << "\n";
    }
    // if (CustomErrorHandler) {
    //   CustomErrorHandler(ss.str().c_str(), wrap(toshow),
    //                      ErrorType::NoDerivative, nullptr, wrap(totrunc),
    //                      wrap(context.ip));
    //   return NewF;
    // }
    if (context.req) {
      EmitFailure("NoTruncate", context.req->getDebugLoc(), context.req,
                  ss.str());
      return NewF;
    }
    llvm::errs() << "mod: " << *totrunc->getParent() << "\n";
    llvm::errs() << *totrunc << "\n";
    llvm_unreachable("attempting to truncate function without definition");
  }

  ValueToValueMapTy originalToNewFn;

  for (auto i = totrunc->arg_begin(), j = NewF->arg_begin();
       i != totrunc->arg_end();) {
    originalToNewFn[i] = j;
    j->setName(i->getName());
    ++j;
    ++i;
  }

  SmallVector<ReturnInst *, 4> Returns;
  CloneFunctionInto(NewF, totrunc, originalToNewFn,
                    CloneFunctionChangeType::LocalChangesOnly, Returns, "",
                    nullptr);

  NewF->setLinkage(Function::LinkageTypes::InternalLinkage);

  TruncateGenerator handle(originalToNewFn, truncation, totrunc, NewF, *this,
                           root);
  for (auto &BB : *totrunc)
    for (auto &I : BB)
      handle.visit(&I);

  if (llvm::verifyFunction(*NewF, &llvm::errs())) {
    llvm::errs() << *totrunc << "\n";
    llvm::errs() << *NewF << "\n";
    report_fatal_error("function failed verification (5)");
  }

  return NewF;
}

void RaptorLogic::clear() {
  // PPC.clear();
}
