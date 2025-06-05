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
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/Instrumentation.h"
#include <array>
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
    Args.push_back(B.getInt64(truncation.getTo().getExponentWidth()));
    Args.push_back(B.getInt64(truncation.getTo().getSignificandWidth()));
    Args.push_back(B.getInt64(truncation.getMode()));
    Args.push_back(LocStr);
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
  // TODO is there some linker hackery that can merge the symbols with the same
  // content at linking time?
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

// TODO we should add an integer parameter to the count function and pass in the
// instruction cost.
class CountGenerator : public llvm::InstVisitor<CountGenerator> {
private:
  FloatRepresentation FR;
  LLVMContext &Ctx;
  Module &M;
  Function *CountFunc;

public:
  CountGenerator(FloatRepresentation FR, Function *F)
      : FR(FR), Ctx(F->getContext()), M(*F->getParent()) {
    CountFunc = getCountFunc();
  }

  Function *getCountFunc() {
    auto MangledName =
        std::string(RaptorFPRTPrefix) + FR.getMangling() + "_count";
    auto F = M.getFunction(MangledName);
    if (!F) {
      SmallVector<Type *, 4> ArgTypes;
      IRBuilder<> B(Ctx);
      FunctionType *FnTy =
          FunctionType::get(B.getVoidTy(), ArgTypes, /*is_vararg*/ false);
      F = Function::Create(FnTy, Function::ExternalLinkage, MangledName, M);
    }
    return F;
  }

  void flop(Instruction &I) {
    IRBuilder B(&I);
    B.CreateCall(CountFunc);
  }

  void visitFCmpInst(llvm::FCmpInst &CI) { flop(CI); }

  Type *getFloatType() { return FR.getBuiltinType(Ctx); }

  void visitCastInst(llvm::CastInst &CI) {
    auto src = CI.getOperand(0);
    if (CI.getSrcTy() == getFloatType() || CI.getDestTy() == getFloatType()) {
      if (isa<Constant>(src))
        return;
      if (Instruction::FPToUI <= CI.getOpcode() &&
          CI.getOpcode() <= Instruction::FPExt)
        flop(CI);
    }
  }

  void visitBinaryOperator(llvm::BinaryOperator &BO) {
    auto oldLHS = BO.getOperand(0);
    auto oldRHS = BO.getOperand(1);

    if (oldLHS->getType() != getFloatType() &&
        oldRHS->getType() != getFloatType())
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

    flop(BO);

    return;
  }

  bool handleIntrinsic(llvm::CallBase &CI, Intrinsic::ID ID) {
    if (isDbgInfoIntrinsic(ID))
      return true;

    bool hasFromType = false;
    for (unsigned i = 0; i < CI.arg_size(); ++i)
      if (CI.getOperand(i)->getType() == getFloatType())
        hasFromType = true;
    if (CI.getType() == getFloatType()) {
      hasFromType = true;
    }

    if (!hasFromType)
      return false;

    flop(CI);

    return true;
  }

  void visitIntrinsicInst(llvm::IntrinsicInst &II) {
    handleIntrinsic(II, II.getIntrinsicID());
  }

  void visitCallBase(llvm::CallBase &CI) {
    Intrinsic::ID ID;
    StringRef funcName = getFuncNameFromCall(const_cast<CallBase *>(&CI));
    if (isMemFreeLibMFunction(funcName, &ID))
      if (handleIntrinsic(CI, ID))
        return;
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
  ValueToValueMapTy &OriginalToNewFn;
  FloatTruncation Truncation;
  TruncateMode Mode;
  RaptorLogic &Logic;
  LLVMContext &Ctx;

public:
  TruncateGenerator(ValueToValueMapTy &originalToNewFn, Function *oldFunc,
                    Function *newFunc, RaptorLogic &Logic,
                    TruncationConfiguration TC)
      : TruncateUtils(TC.Truncation, newFunc->getParent(), Logic),
        OriginalToNewFn(originalToNewFn), Truncation(TC.Truncation),
        Mode(Truncation.getMode()), Logic(Logic), Ctx(newFunc->getContext()) {

    auto AllocScratch = [&]() {
      // TODO we should check at the end if we never used the scracth we should
      // remove the runtime calls for allocation.
      auto GetName = "get_scratch";
      auto FreeName = "free_scratch";
      auto TruncChangeName = "trunc_change";
      IRBuilder<> B(newFunc->getContext());
      B.SetInsertPointPastAllocas(newFunc);
      SmallVector<Value *> scratchArgs;
      SmallVector<Value *> changePushArgs = {B.getInt64(1)};
      SmallVector<Value *> changePopArgs = {B.getInt64(0)};
      // TODO should be the callsite or the function location itself
      Value *Loc = getUniquedLocStr(
          &*newFunc->getEntryBlock().getFirstNonPHIOrDbgOrAlloca());
      if (TC.NeedTruncChange)
        createFPRTGeneric(B, TruncChangeName, changePushArgs, B.getVoidTy(),
                          Loc);
      if (TC.NeedNewScratch)
        scratch = createFPRTGeneric(B, GetName, scratchArgs, B.getPtrTy(), Loc);
      for (auto &BB : *newFunc) {
        if (ReturnInst *ret = dyn_cast<ReturnInst>(BB.getTerminator())) {
          B.SetInsertPoint(ret);
          if (TC.NeedNewScratch)
            createFPRTGeneric(B, FreeName, scratchArgs, B.getPtrTy(), Loc);
          if (TC.NeedTruncChange)
            createFPRTGeneric(B, "trunc_change", changePopArgs, B.getVoidTy(),
                              Loc);
        }
      }
    };
    if (Truncation.isToFPRT()) {
      if (Mode == TruncOpMode) {
        if (TC.NeedTruncChange || TC.NeedNewScratch)
          AllocScratch();
        if (!TC.NeedNewScratch) {
          // make sure we passed in `void *scratch` as the final parameter
          assert(newFunc->arg_size() == oldFunc->arg_size() + 1);
          scratch = newFunc->getArg(newFunc->arg_size() - 1);
          assert(scratch->getType()->isPointerTy());
        }
      } else if (Mode == TruncOpFullModuleMode) {
        assert(TC.NeedNewScratch);
        assert(!TC.NeedTruncChange);
        // TODO we need to do a call to trunc_change in the module constructor
        AllocScratch();
      }
    }
  }

  void todo(llvm::Instruction &I) {
    if (all_of(I.operands(),
               [&](Use &U) { return U.get()->getType() != fromType; }) &&
        I.getType() != fromType)
      return;

    switch (Mode) {
    case TruncMemMode:
      llvm::errs() << I << "\n";
      EmitFailure("FPEscaping", I.getDebugLoc(), &I, "FP value escapes!");
      break;
    case TruncOpMode:
    case TruncOpFullModuleMode:
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
    switch (Mode) {
    case TruncMemMode:
      if (isa<ConstantFP>(v))
        return createFPRTConstCall(B, v);
      return floatMemTruncate(B, v, Truncation);
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return floatValTruncate(B, v, Truncation);
    }
    llvm_unreachable("Unknown trunc mode");
  }

  Value *expand(IRBuilder<> &B, Value *v) {
    switch (Mode) {
    case TruncMemMode:
      return floatMemExpand(B, v, Truncation);
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return floatValExpand(B, v, Truncation);
    }
    llvm_unreachable("Unknown trunc mode");
  }

  void visitUnaryOperator(UnaryOperator &I) {
    switch (I.getOpcode()) {
    case UnaryOperator::FNeg: {
      if (I.getOperand(0)->getType() != getFromType())
        return;
      if (!Truncation.isToFPRT())
        return;

      auto newI = getNewFromOriginal(&I);
      IRBuilder<> B(newI);
      SmallVector<Value *, 2> Args = {newI->getOperand(0)};
      auto nres = createFPRTOpCall(B, I, newI->getType(), Args);
      nres->takeName(newI);
      nres->copyIRFlags(newI);
      newI->replaceAllUsesWith(nres);
      newI->eraseFromParent();
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
    switch (Mode) {
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
      if (Truncation.isToFPRT())
        nres = createFPRTOpCall(B, CI, B.getInt1Ty(), Args);
      else
        nres =
            cast<FCmpInst>(B.CreateFCmp(CI.getPredicate(), truncLHS, truncRHS));
      nres->takeName(newI);
      nres->copyIRFlags(newI);
      newI->replaceAllUsesWith(nres);
      newI->eraseFromParent();
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
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
    switch (Mode) {
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
        nres->takeName(newI);
        nres->copyIRFlags(newI);
        newI->replaceUsesWithIf(nres,
                                [&](Use &U) { return U.getUser() != nres; });
        OriginalToNewFn[const_cast<const Value *>(cast<Value>(&CI))] = nres;
      }
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return;
    }
  }
  void visitSelectInst(llvm::SelectInst &SI) {
    switch (Mode) {
    case TruncMemMode: {
      if (SI.getType() != getFromType())
        return;
      auto newI = getNewFromOriginal(&SI);
      IRBuilder<> B(newI);
      auto newT = truncate(B, getNewFromOriginal(SI.getTrueValue()));
      auto newF = truncate(B, getNewFromOriginal(SI.getFalseValue()));
      auto nres = cast<SelectInst>(
          B.CreateSelect(getNewFromOriginal(SI.getCondition()), newT, newF));
      nres->takeName(newI);
      nres->copyIRFlags(newI);
      newI->replaceAllUsesWith(expand(B, nres));
      newI->eraseFromParent();
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
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
    if (Truncation.isToFPRT()) {
      SmallVector<Value *, 2> Args({newLHS, newRHS});
      nres = createFPRTOpCall(B, BO, Truncation.getToType(Ctx), Args);
    } else {
      nres = cast<Instruction>(B.CreateBinOp(BO.getOpcode(), newLHS, newRHS));
    }
    nres->takeName(newI);
    nres->copyIRFlags(newI);
    newI->replaceAllUsesWith(expand(B, nres));
    newI->eraseFromParent();
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
    if (Truncation.isToFPRT()) {
      nres = intr = createFPRTOpCall(B, CI, retTy, new_ops);
    } else {
      // TODO check that the intrinsic is overloaded
      nres = intr =
          createIntrinsicCall(B, ID, retTy, new_ops, &CI, CI.getName());
    }
    if (newI->getType() == getFromType())
      nres = expand(B, nres);
    intr->copyIRFlags(newI);
    newI->replaceAllUsesWith(nres);
    newI->eraseFromParent();
    return true;
  }

  void visitIntrinsicInst(llvm::IntrinsicInst &II) {
    handleIntrinsic(II, II.getIntrinsicID());
  }

  void visitReturnInst(llvm::ReturnInst &I) {
    switch (Mode) {
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
    switch (Mode) {
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
      break;
    default:
      llvm_unreachable("Unknown trunc mode");
    }
    return;
  }

  llvm::Value *getNewFromOriginal(llvm::Value *v) {
    auto found = OriginalToNewFn.find(v);
    assert(found != OriginalToNewFn.end());
    return found->second;
  }

  llvm::Instruction *getNewFromOriginal(llvm::Instruction *v) {
    return cast<Instruction>(getNewFromOriginal((llvm::Value *)v));
  }

  Value *GetShadow(RequestContext &ctx, Value *v, bool WillPassScratch) {
    if (auto F = dyn_cast<Function>(v))
      return Logic.CreateTruncateFunc(
          ctx, F,
          TruncationConfiguration{Truncation, Mode, !WillPassScratch, false,
                                  WillPassScratch});
    llvm::errs() << " unknown get truncated func: " << *v << "\n";
    llvm_unreachable("unknown get truncated func");
    return v;
  }

  struct FunctionToTrunc {
    Function *Func;
    bool IsCallback;
    unsigned ArgNo;
    unsigned getCallbackArgNo() {
      assert(isCallbackFunc());
      return ArgNo;
    }
    bool isCallbackFunc() { return IsCallback; }
  };

  SmallVector<FunctionToTrunc, 1> getFunctionToTruncate(llvm::CallBase &CI) {
    SmallVector<FunctionToTrunc, 1> ToTrunc;
    auto MaybeInsert = [&](Function *F, bool IsCallback, unsigned ArgNo = 0) {
      if (!F) {
        switch (Mode) {
        case TruncMemMode:
        case TruncOpMode:
          EmitWarning("FPNoFollow", CI,
                      "Will not follow FP through this indirect call.", CI);
          break;
        default:
          llvm_unreachable("Unknown trunc mode");
        }
        return;
      }
      if (F->isDeclaration()) {
        switch (Mode) {
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
        return;
      }
      ToTrunc.push_back(FunctionToTrunc{F, IsCallback, ArgNo});
    };

    Function *Callee = CI.getCalledFunction();
    MaybeInsert(Callee, false);

    if (!Callee)
      return ToTrunc;
    if (!Callee->isDeclaration())
      return ToTrunc;

    MDNode *CallbackMD = Callee->getMetadata(LLVMContext::MD_callback);
    if (CallbackMD) {
      for (const MDOperand &Op : CallbackMD->operands()) {
        MDNode *OpMD = cast<MDNode>(Op.get());
        auto *CBCalleeIdxAsCM = cast<ConstantAsMetadata>(OpMD->getOperand(0));
        uint64_t CBCalleeIdx =
            cast<ConstantInt>(CBCalleeIdxAsCM->getValue())->getZExtValue();
        MaybeInsert(dyn_cast<Function>(CI.getArgOperand(CBCalleeIdx)), true,
                    CBCalleeIdx);
      }
    }

    return ToTrunc;
  }

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

    if (Mode != TruncOpMode && Mode != TruncMemMode)
      return;

    RequestContext ctx(&CI, &BuilderZ);
    auto FTTs = getFunctionToTruncate(CI);
    auto NeedDirectCall = [&](auto FTT) {
      return scratch && Mode == TruncOpMode && isa<CallInst>(&CI) &&
             !FTT.isCallbackFunc();
    };
    for (auto &FTT : FTTs) {
      assert(FTT.Func && !FTT.Func->empty());
      if (!NeedDirectCall(FTT)) {
        auto val = GetShadow(ctx, getNewFromOriginal(FTT.Func), false);
        if (FTT.isCallbackFunc()) {
          newCall->setArgOperand(FTT.getCallbackArgNo(), val);
        } else {
          newCall->setCalledOperand(val);
        }
      }
    }
    for (auto &FTT : FTTs) {
      assert(FTT.Func && !FTT.Func->empty());
      if (NeedDirectCall(FTT)) {
        auto val = GetShadow(ctx, getNewFromOriginal(FTT.Func), true);
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
      }
    }
  }
  void visitPHINode(llvm::PHINode &PN) {
    switch (Mode) {
    case TruncMemMode: {
      if (PN.getType() != getFromType())
        return;
      auto NewPN = cast<llvm::PHINode>(getNewFromOriginal(&PN));
      IRBuilder<> B(&*NewPN->getParent()
                          ->getParent()
                          ->getEntryBlock()
                          .getFirstNonPHIIt());
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
      break;
    default:
      llvm_unreachable("Unknown trunc mode");
    }
  }
};

bool RaptorLogic::CreateTruncateValue(RequestContext context, Value *v,
                                      FloatTruncation Truncation,
                                      bool isTruncate) {
  assert(context.req && context.ip);

  if (!Truncation.getTo().isMPFR())
    EmitFailure("NoMPFR", context.req->getDebugLoc(), context.req,
                "trunc value needs target type to be MPFR");

  IRBuilderBase &B = *context.ip;

  Value *converted = nullptr;
  TruncateUtils TU(Truncation, B.GetInsertBlock()->getParent()->getParent(),
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

bool RaptorLogic::CountInFunc(llvm::Function *F, FloatRepresentation FR) {

  CountGenerator Handle(FR, F);
  for (auto &BB : *F)
    for (auto &I : BB)
      Handle.visit(&I);

  if (llvm::verifyFunction(*F, &llvm::errs())) {
    llvm::errs() << *F << "\n";
    report_fatal_error("function failed verification (5)");
  }

  return true;
}

llvm::Function *RaptorLogic::CreateTruncateFunc(RequestContext Context,
                                                llvm::Function *ToTrunc,
                                                TruncationConfiguration TC) {
  TruncateCacheKey tup(ToTrunc, TC);
  if (TruncateCachedFunctions.find(tup) != TruncateCachedFunctions.end()) {
    return TruncateCachedFunctions.find(tup)->second;
  }

  IRBuilder<> B(ToTrunc->getContext());

  FunctionType *OrigFTy = ToTrunc->getFunctionType();
  SmallVector<Type *, 4> Params;

  for (unsigned i = 0; i < OrigFTy->getNumParams(); ++i) {
    Params.push_back(OrigFTy->getParamType(i));
  }

  if (TC.ScratchFromArgs) {
    // void *scratch
    Params.push_back(B.getPtrTy());
  }

  Type *NewTy = ToTrunc->getReturnType();

  FunctionType *FTy = FunctionType::get(NewTy, Params, ToTrunc->isVarArg());
  std::string truncName = std::string("__raptor_done_truncate_") + TC.mangle() +
                          "_" + ToTrunc->getName().str();
  Function *NewF = Function::Create(FTy, ToTrunc->getLinkage(), truncName,
                                    ToTrunc->getParent());

  if (TC.Mode != TruncOpFullModuleMode)
    NewF->setLinkage(Function::LinkageTypes::InternalLinkage);

  TruncateCachedFunctions[tup] = NewF;

  if (ToTrunc->empty()) {
    std::string s;
    llvm::raw_string_ostream ss(s);
    ss << "No truncate mode found for " + ToTrunc->getName() << "\n";
    // llvm::Value *toshow = totrunc;
    if (Context.req) {
      // toshow = context.req;
      ss << " at context: " << *Context.req;
    } else {
      ss << *ToTrunc << "\n";
    }
    // if (CustomErrorHandler) {
    //   CustomErrorHandler(ss.str().c_str(), wrap(toshow),
    //                      ErrorType::NoDerivative, nullptr, wrap(totrunc),
    //                      wrap(context.ip));
    //   return NewF;
    // }
    if (Context.req) {
      EmitFailure("NoTruncate", Context.req->getDebugLoc(), Context.req,
                  ss.str());
      return NewF;
    }
    llvm::errs() << "mod: " << *ToTrunc->getParent() << "\n";
    llvm::errs() << *ToTrunc << "\n";
    llvm_unreachable("attempting to truncate function without definition");
  }

  ValueToValueMapTy originalToNewFn;

  for (auto i = ToTrunc->arg_begin(), j = NewF->arg_begin();
       i != ToTrunc->arg_end();) {
    originalToNewFn[i] = j;
    j->setName(i->getName());
    ++j;
    ++i;
  }

  SmallVector<ReturnInst *, 4> Returns;
  CloneFunctionInto(NewF, ToTrunc, originalToNewFn,
                    CloneFunctionChangeType::LocalChangesOnly, Returns, "",
                    nullptr);

  NewF->setLinkage(Function::LinkageTypes::InternalLinkage);

  TruncateGenerator Handle(originalToNewFn, ToTrunc, NewF, *this, TC);
  for (auto &BB : *ToTrunc)
    for (auto &I : BB)
      Handle.visit(&I);

  if (llvm::verifyFunction(*NewF, &llvm::errs())) {
    llvm::errs() << *ToTrunc << "\n";
    llvm::errs() << *NewF << "\n";
    report_fatal_error("function failed verification (5)");
  }

  return NewF;
}

void RaptorLogic::clear() {
  // PPC.clear();
}
