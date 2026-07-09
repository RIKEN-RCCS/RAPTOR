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
#include "llvm/IR/AbstractCallSite.h"
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
                               TruncationConfiguration truncation) {
  if (truncation.isToFPRT())
    return v;

  Type *toTy = truncation.getToType(B.getContext());
  if (auto vty = dyn_cast<VectorType>(v->getType()))
    toTy = VectorType::get(toTy, vty->getElementCount());
  return B.CreateFPTrunc(v, toTy, "raptor_trunc");
}

static Value *floatValExpand(IRBuilderBase &B, Value *v,
                             TruncationConfiguration truncation) {
  if (truncation.isToFPRT())
    return v;

  Type *fromTy = truncation.getFromType(B.getContext());
  if (auto vty = dyn_cast<VectorType>(v->getType()))
    fromTy = VectorType::get(fromTy, vty->getElementCount());
  return B.CreateFPExt(v, fromTy, "raptor_exp");
}

static Value *floatMemTruncate(IRBuilderBase &B, Value *v,
                               TruncationConfiguration truncation) {
  return v;
}

static Value *floatMemExpand(IRBuilderBase &B, Value *v,
                             TruncationConfiguration truncation) {
  return v;
}

class TruncateUtils {
protected:
  TruncationConfiguration TC;
  llvm::Module *M;
  Type *fromType;
  Type *toType;
  LLVMContext &ctx;
  RaptorLogic &Logic;
  Value *UnknownLoc;
  Value *scratch = nullptr;
  CustomArgsTy CustomArgs;
  std::string RTName;

private:
  std::string getOriginalFPRTName(std::string Name) {
    return std::string(RaptorPrefix) + RTName + "_original_" + TC.mangleFrom() +
           "_" + Name;
  }
  std::string getFPRTName(std::string Name) {
    return std::string(RaptorPrefix) + RTName + "_" + TC.mangleFrom() + "_" +
           Name;
  }
  std::string getVecFPRTName(std::string Name, bool isScalable, 
                             unsigned fixedLen) {
    return std::string(RaptorPrefix) + RTName + "_vec" + 
           (isScalable? "nx" : "") + Twine(fixedLen).str() + "x_" + 
           TC.mangleFrom() + "_" + Name;
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

  // Get the element count of vector operand/argument of vector op/func to for
  // conversion to FPRT
  ElementCount getVecFPRTFuncEC(Instruction &I, SmallVectorImpl<Value *> &Args,
                              llvm::VectorType *RetTy) {
    ElementCount EC = RetTy->getElementCount();
    bool hasVecFromType = false;
    bool allVecSizeMatch = true;
    if (!Args.empty() && isVecOfFromType(Args[0]->getType())) {
      hasVecFromType = true;
      allVecSizeMatch = 
        (EC == cast<VectorType>(Args[0]->getType())->getElementCount());
    }
    if (dyn_cast<UnaryOperator>(&I)) {
      if (!hasVecFromType)
        llvm_unreachable("Unexpected unary op for vec conversion to FPRT");
      assert(RetTy == Args[0]->getType());
    } else if (dyn_cast<BinaryOperator>(&I)) {
      if (!hasVecFromType)
        llvm_unreachable("Unexpected binary op for vec conversion to FPRT");
      assert(Args[0]->getType() == Args[1]->getType());
    } else if (dyn_cast<FCmpInst>(&I)) {
      if (!hasVecFromType)
        llvm_unreachable("Unexpected fcmp inst for vec conversion to FPRT");
      assert(Args[0]->getType() == Args[1]->getType());
    } else if (dyn_cast<CallInst>(&I)) { // Include IntrinsicInst
      for (auto it = Args.begin(); it != Args.end(); ++it) {
        if ((*it)->getType()->isVectorTy()) {
          allVecSizeMatch = allVecSizeMatch && 
            (EC == cast<VectorType>((*it)->getType())->getElementCount());
          hasVecFromType = hasVecFromType || 
            isVecOfFromType((*it)->getType());
        }
      }
    }
    if (!hasVecFromType)
      llvm_unreachable(
        "Unexpected inst without vec fromTy param for vec conversion to FPRT");
    if (!allVecSizeMatch)
      llvm_unreachable(
        "Unexpected inst with different vec sizes for vec conversion to FPRT");
    assert(hasVecFromType && allVecSizeMatch);
    if (dyn_cast<VPIntrinsic>(&I))
      llvm_unreachable(
        "Unexpected vector predicate intrinsic for vec conversion to FPRT");
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I)) {
      std::string Name = Intrinsic::getBaseName(II->getIntrinsicID()).str();
      if (Name.substr(0,11) == "llvm.vector")
        llvm_unreachable(
          "Unexpected vec reduce or manip intr for vec conversion to FPRT");
      if (Name.substr(0,11) == "llvm.matrix")
        llvm_unreachable(
          "Unexpected matrix intrinsic for vec conversion to FPRT");
    }
    return EC;
  }

  // Get scalar intrinsic func name
  std::string getScalarIntrinsicFuncName(IntrinsicInst &II) {
    llvm::Intrinsic::ID ID = II.getIntrinsicID();
    assert(ID != Intrinsic::not_intrinsic);
    // Get the overload types in vecTypes
    SmallVector<Type *, 4> vecTypes;
    SmallVector<Intrinsic::IITDescriptor, 8> Table;
    getIntrinsicInfoTableEntries(ID, Table);
    ArrayRef<Intrinsic::IITDescriptor> TableRef = Table;
    Intrinsic::matchIntrinsicSignature(
      II.getCalledFunction()->getFunctionType(), TableRef, vecTypes);
    SmallVector<Type *, 4> scalarTypes;
    for (auto ty : vecTypes) {
      if (auto vecType = dyn_cast<VectorType>(ty)) {
        scalarTypes.push_back(vecType->getScalarType());
      } else {
        scalarTypes.push_back(ty);
      }
    }
    std::string Name = Intrinsic::getName(ID, scalarTypes, II.getModule());
    Name = "intr_" + Name;
    for (auto &C : Name)
      if (C == '.')
        C = '_';
    return Name;
  }

  Value *InsertScalarFPRTCall(llvm::IRBuilderBase &B, std::string Name, 
                              Value * startingIndex, Value * vecRet,
                              Function * F, SmallVectorImpl<Value *> &Args,
                              llvm::VectorType *RetTy, Value *LocStr) {
    llvm::Type * scalarRetTy = RetTy->getScalarType();
    auto fixedVecLen = RetTy->getElementCount().getKnownMinValue();
    // Each insertElement creates a new value
    SmallVector<Value *, 4> vecRets;
    vecRets.push_back(vecRet);
    for (auto i = 0; i < fixedVecLen; ++i) {
      // vecId = startingIndex + i;
      Value * vecId = (startingIndex == nullptr)? B.getInt64(i) :
                      (i == 0) ? startingIndex :
                      B.CreateAdd(startingIndex, B.getInt64(i));
      // Construct input arguments to the scalar FPRT call
      SmallVector<Value *, 4> scalarArgs;
      for (auto j = 0; j < Args.size(); ++j) {
        if (Args[j]->getType()->isVectorTy()) { 
          // scalarArg = Args[k][vecId]; 
          Value * scalarArg = B.CreateExtractElement(F->getArg(j), vecId);
          // scalarArgs[k] = scalarArg;
          scalarArgs.push_back(scalarArg);
        } else {
          // scalarArgs[k] = Args[k];
          scalarArgs.push_back(F->getArg(j));
        }
      }
      // scalarRet = scalarFPRTCall(scalarArgs);
      CallInst *scalarRet = createFPRTGeneric(B, Name, scalarArgs, scalarRetTy,
                                              LocStr);
      // Forward extra args from vec to scalar call
      for (auto k = Args.size(); k < scalarRet->arg_size(); ++k) {
        scalarRet->setArgOperand(k, F->getArg(k));
      }
      // vecRet[vecId] = scalarRet;
      vecRets.push_back(B.CreateInsertElement(vecRets[i], scalarRet, vecId));
    }
    return vecRets.back();
  }

  // Create a function which contains a loop over truncated scalar version of
  // the original vectorized operations
  Function *CreateVecFPRTFunc(llvm::IRBuilderBase &B, std::string vecName, 
                              std::string scalarName,
                              SmallVectorImpl<Value *> &Args,
                              llvm::VectorType *RetTy, Value *LocStr) {
    auto isScalable = RetTy->getElementCount().isScalable();
    auto fixedVecLen = RetTy->getElementCount().getKnownMinValue();
    auto MangledName = getVecFPRTName(vecName, isScalable, fixedVecLen);
    auto F = M->getFunction(MangledName);
    
    if (!F) {
      SmallVector<Type *, 4> ArgTypes;
      for (auto Arg : Args)
        ArgTypes.push_back(Arg->getType());
      for (auto CustomArg : CustomArgs)
        ArgTypes.push_back(CustomArg->getType());
      ArgTypes.push_back(LocStr->getType());
      ArgTypes.push_back(scratch->getType());
      FunctionType *FnTy =
          FunctionType::get(RetTy, ArgTypes, /*is_vararg*/ false);
      F = Function::Create(FnTy, Function::WeakAnyLinkage, MangledName, M);
      if (isScalable) {
        EmitWarning("UntestedTruncation", *F, 
                    "Raptor FPRT func with scalable vector operand has not ",
                    "been tested.", *F);
      }
    }
    if (F->isDeclaration()) {
      BasicBlock *Entry = BasicBlock::Create(F->getContext(), "entry", F);
      IRBuilder<> vecFuncB(Entry);
      if (!isScalable) {
        // vecRet[fixedVecLen];
        Value *vecRet = PoisonValue::get(RetTy);
        // Insert fixedVecLen instances of scalar FPRT call
        vecRet = InsertScalarFPRTCall(vecFuncB, scalarName, nullptr, vecRet, F,
                                      Args, RetTy, LocStr);
        vecFuncB.CreateRet(vecRet);
      } else {
        BasicBlock *Body = BasicBlock::Create(F->getContext(), "loop.body", F);
        BasicBlock *Exit = BasicBlock::Create(F->getContext(), "exit", F);
        // vecLen = vscale * fixedVecLen
        Value *vscale = vecFuncB.CreateIntrinsic(Intrinsic::vscale, 
                                                 {vecFuncB.getInt64Ty()}, {});
        Value *vecLen = vecFuncB.CreateMul(vscale, 
                                           vecFuncB.getInt64(fixedVecLen));
        vecFuncB.CreateBr(Body);
        // for (
        vecFuncB.SetInsertPoint(Body);
        //      i = 0;;) {
        PHINode * i = vecFuncB.CreatePHI(vecFuncB.getInt64Ty(), 2);
        i->addIncoming(vecFuncB.getInt64(0), Entry);
        //   vecRet[vecLen];
        PHINode *vecPHI = vecFuncB.CreatePHI(RetTy, 2);
        vecPHI->addIncoming(PoisonValue::get(RetTy), Entry);
        //   Insert fixedVecLen instances of scalar FPRT call
        Value * vecRet = InsertScalarFPRTCall(vecFuncB, scalarName, i, vecPHI,
                                              F, Args, RetTy, LocStr);
        vecPHI->addIncoming(vecRet, Body);
        //   i += fixedVecLen;
        Value * incI = vecFuncB.CreateAdd(i, vecFuncB.getInt64(fixedVecLen));
        i->addIncoming(incI, Body);
        //   if (i == vecLen) break;
        Value * stopCond = vecFuncB.CreateCmp(CmpInst::Predicate::ICMP_EQ, i, 
                                              vecLen);
        vecFuncB.CreateCondBr(stopCond, Exit, Body);
        // }
        vecFuncB.SetInsertPoint(Exit);
        // return vecRet;
        vecFuncB.CreateRet(vecRet);
      }
      F->setLinkage(GlobalValue::WeakODRLinkage);
      // Clear invalidated debug metadata now that we defined the function
      F->clearMetadata();
    }
    return F;
  }

  // Create call to truncated vectorized operations (through loop of scalar)
  CallInst *createVecFPRTFuncCall(llvm::IRBuilderBase &B, ElementCount &EC, 
                                  std::string vecName, std::string scalarName,
                                  SmallVectorImpl<Value *> &Args, 
                                  llvm::VectorType *RetTy, Value *LocStr) {
    auto MangledName = getVecFPRTName(vecName, EC.isScalable(), 
                                      EC.getKnownMinValue());
    auto F = M->getFunction(MangledName);
    if (!F) {
      F = CreateVecFPRTFunc(B, vecName, scalarName, Args, RetTy, LocStr);
    }
    SmallVector<Value *, 4> vecArgs;
    for (auto Arg : Args) {
      vecArgs.push_back(Arg);
    }
    vecArgs.append(CustomArgs);
    vecArgs.push_back(LocStr);
    vecArgs.push_back(scratch);
    // Explicitly assign a dbg location if it didn't exist, as the FPRT
    // functions are inlineable and the backend fails if the callsite does not
    // have dbg metadata
    // TODO consider using InstrumentationIRBuilder
    Function *ContainingF = B.GetInsertBlock()->getParent();
    if (!B.getCurrentDebugLocation() && ContainingF->getSubprogram())
      B.SetCurrentDebugLocation(DILocation::get(ContainingF->getContext(), 0, 0,
                                                ContainingF->getSubprogram()));
    auto *CI = cast<CallInst>(B.CreateCall(F, vecArgs));

    return CI;
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
    Args.append(CustomArgs);
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

  TruncateUtils(TruncationConfiguration TC, Module *M, RaptorLogic &Logic)
      : TC(TC), M(M), ctx(M->getContext()), Logic(Logic),
        CustomArgs(TC.CustomArgs), RTName(TC.RTName) {
    fromType = TC.getFromType(M->getContext());
    toType = TC.getToType(M->getContext());
    UnknownLoc = getUniquedLocStr(nullptr);
    scratch = ConstantPointerNull::get(PointerType::get(M->getContext(), 0));
  }

  Type *getFromType() { return fromType; }

  Type *getToType() { return toType; }

  bool isVecOfFromType(llvm::Type * Ty) {
    return Ty->isVectorTy() && (Ty->getScalarType() == getFromType());
  }

  bool hasVecOfFromType(SmallVectorImpl<Value *> &ArgsIn) {
    bool found = false;
    for (auto Args : ArgsIn) {
      found = found || isVecOfFromType(Args->getType());
    }
    return found;
  }

  VectorType *getVecToType(ElementCount EC) {
    return VectorType::get(toType, EC);
  }

  bool isVecOfConst(Value * V) {
    return V->getType()->isVectorTy() && dyn_cast<Constant>(V);
  }

  CallInst *createFPRTConstCall(llvm::IRBuilderBase &B, Value *V) {
    assert(V->getType()->getScalarType() == getFromType());
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    if (isVecOfFromType(V->getType())) {
      assert(isVecOfConst(V));
      ElementCount EC = cast<VectorType>(V->getType())->getElementCount();
      return createVecFPRTFuncCall(B, EC, "const", "const", Args, 
                                   getVecToType(EC), UnknownLoc);
    }
    return createFPRTGeneric(B, "const", Args, getToType(), UnknownLoc);
  }
  CallInst *createFPRTNewCall(llvm::IRBuilderBase &B, Value *V) {
    assert(V->getType()->getScalarType() == getFromType());
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    if (isVecOfFromType(V->getType())) {
      ElementCount EC = cast<VectorType>(V->getType())->getElementCount();
      return createVecFPRTFuncCall(B, EC, "new", "new", Args, getVecToType(EC),
                                   UnknownLoc);
    }
    return createFPRTGeneric(B, "new", Args, getToType(), UnknownLoc);
  }
  CallInst *createFPRTGetCall(llvm::IRBuilderBase &B, Value *V) {
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    if (isVecOfFromType(V->getType())) {
      ElementCount EC = cast<VectorType>(V->getType())->getElementCount();
      return createVecFPRTFuncCall(B, EC, "get", "get", Args, getVecToType(EC),
                                   UnknownLoc);
    }
    return createFPRTGeneric(B, "get", Args, getToType(), UnknownLoc);
  }
  CallInst *createFPRTDeleteCall(llvm::IRBuilderBase &B, Value *V) {
    SmallVector<Value *, 1> Args;
    Args.push_back(V);
    if (isVecOfFromType(V->getType())) {
      ElementCount EC = cast<VectorType>(V->getType())->getElementCount();
      return createVecFPRTFuncCall(B, EC, "delete", "delete", Args, 
                                   VectorType::get(B.getVoidTy(), EC), 
                                   UnknownLoc);
    }
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
        Name = "func_" + clipLibMFuncTrailingfl(std::string(F->getName()));
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
    if (hasVecOfFromType(ArgsIn)) {
      if (RetTy->isVectorTy()) {
        ElementCount EC = getVecFPRTFuncEC(I, ArgsIn, cast<VectorType>(RetTy));
        std::string scalarName = Name;
        if (auto II = dyn_cast<IntrinsicInst>(&I)) {
          if (Intrinsic::isOverloaded(II->getIntrinsicID())) {
            scalarName = getScalarIntrinsicFuncName(*II);
          }
        }
        return createVecFPRTFuncCall(B, EC, Name, scalarName, ArgsIn, 
                                     cast<VectorType>(RetTy), 
                                     getUniquedLocStr(&I));
      } else {
        llvm_unreachable("Unexpected reduction inst for conversion to FPRT");
      }
    }
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

  Type *getFloatType() { return FR.getBuiltinType(Ctx); }

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
  TruncationConfiguration TC;
  TruncateMode Mode;
  RaptorLogic &Logic;
  LLVMContext &Ctx;

public:
  TruncateGenerator(ValueToValueMapTy &originalToNewFn, Function *oldFunc,
                    Function *newFunc, RaptorLogic &Logic,
                    TruncationConfiguration TC)
      : TruncateUtils(TC, newFunc->getParent(), Logic),
        OriginalToNewFn(originalToNewFn), TC(TC), Mode(TC.getMode()),
        Logic(Logic), Ctx(newFunc->getContext()) {

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
    if (TC.isToFPRT()) {
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
      if (isa<ConstantFP>(v) || isVecOfConst(v))
        return createFPRTConstCall(B, v);
      return floatMemTruncate(B, v, TC);
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return floatValTruncate(B, v, TC);
    }
    llvm_unreachable("Unknown trunc mode");
  }

  Value *expand(IRBuilder<> &B, Value *v) {
    switch (Mode) {
    case TruncMemMode:
      return floatMemExpand(B, v, TC);
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return floatValExpand(B, v, TC);
    }
    llvm_unreachable("Unknown trunc mode");
  }

  void visitUnaryOperator(UnaryOperator &I) {
    switch (I.getOpcode()) {
    case UnaryOperator::FNeg: {
      if (I.getOperand(0)->getType()->getScalarType() != getFromType())
        return;
      if (!TC.isToFPRT())
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
      if (LHS->getType()->getScalarType() != getFromType())
        return;

      auto newI = getNewFromOriginal(&CI);
      IRBuilder<> B(newI);
      auto truncLHS = truncate(B, LHS);
      auto truncRHS = truncate(B, RHS);

      SmallVector<Value *, 2> Args;
      Args.push_back(truncLHS);
      Args.push_back(truncRHS);
      Instruction *nres;
      if (TC.isToFPRT()) {
        assert(newI->getType()->isVectorTy() || 
               (newI->getType() == B.getInt1Ty()));
        nres = createFPRTOpCall(B, CI, newI->getType(), Args);
      } else
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
      if (CI.getSrcTy()->getScalarType() == getFromType()) {
        IRBuilder<> B(newI);
        if (isa<Constant>(newSrc) || isVecOfConst(newSrc))
          return;
        newI->setOperand(0, createFPRTGetCall(B, newSrc));
        EmitWarning("FPNoFollow", CI, "Will not follow FP through this cast.",
                    CI);
      } else if (CI.getDestTy()->getScalarType() == getFromType()) {
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
      if (SI.getType()->getScalarType() != getFromType())
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
  void visitExtractElementInst(llvm::ExtractElementInst &EEI) { 
    switch (Mode) {
    case TruncMemMode: {
      if (EEI.getType()->getScalarType() != getFromType())
        return;
      auto newI = getNewFromOriginal(&EEI);
      IRBuilder<> B(newI);
      // VectorType that elements are being extracted from
      if (isVecOfConst(newI->getOperand(0)))
        newI->setOperand(0, createFPRTConstCall(B, newI->getOperand(0)));
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return;
    }
    llvm_unreachable("");
  }
  void visitInsertElementInst(llvm::InsertElementInst &EEI) { 
    switch (Mode) {
    case TruncMemMode: {
      if (EEI.getType()->getScalarType() != getFromType())
        return;
      auto newI = getNewFromOriginal(&EEI);
      IRBuilder<> B(newI);
      // VectorType that elements are being inserted to
      if (isVecOfConst(newI->getOperand(0)))
        newI->setOperand(0, createFPRTConstCall(B, newI->getOperand(0)));
      // Inserted scalar value
      if (isa<ConstantFP>(newI->getOperand(1)))
        newI->setOperand(1, createFPRTConstCall(B, newI->getOperand(1)));
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return;
    }
    llvm_unreachable("");
  }
  void visitShuffleVectorInst(llvm::ShuffleVectorInst &EEI) { 
    switch (Mode) {
    case TruncMemMode: {
      if (EEI.getType()->getScalarType() != getFromType())
        return;
      auto newI = getNewFromOriginal(&EEI);
      IRBuilder<> B(newI);
      // First VectorType that is being shuffled
      if (isVecOfConst(newI->getOperand(0)))
        newI->setOperand(0, createFPRTConstCall(B, newI->getOperand(0)));
      // Second VectorType that is being shuffled
      if (isVecOfConst(newI->getOperand(1)))
        newI->setOperand(1, createFPRTConstCall(B, newI->getOperand(1)));
      return;
    }
    case TruncOpMode:
    case TruncOpFullModuleMode:
      return;
    }
    llvm_unreachable("");
  }
  void visitExtractValueInst(llvm::ExtractValueInst &EEI) { return; }
  void visitInsertValueInst(llvm::InsertValueInst &EEI) { return; }
  void visitBinaryOperator(llvm::BinaryOperator &BO) {
    auto oldLHS = BO.getOperand(0);
    auto oldRHS = BO.getOperand(1);

    if (oldLHS->getType()->getScalarType() != getFromType() &&
        oldRHS->getType()->getScalarType() != getFromType())
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
    if (TC.isToFPRT()) {
      SmallVector<Value *, 2> Args({newLHS, newRHS});
      Type *toType = isVecOfFromType(newI->getType()) ?
                     getVecToType(cast<VectorType>(newI->getType())
                                  ->getElementCount()) :
                     getToType();
      nres = createFPRTOpCall(B, BO, toType, Args);
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
      if (orig_ops[i]->getType()->getScalarType() == getFromType()) {
        new_ops[i] = truncate(B, getNewFromOriginal(orig_ops[i]));
        hasFromType = true;
      } else {
        new_ops[i] = getNewFromOriginal(orig_ops[i]);
      }
    }
    Type *retTy = CI.getType();
    if (CI.getType()->getScalarType() == getFromType()) {
      hasFromType = true;
      retTy = isVecOfFromType(CI.getType())? 
              getVecToType(cast<VectorType>(CI.getType())->getElementCount()) :
              getToType();
    }

    if (!hasFromType)
      return false;

    Instruction *intr = nullptr;
    Value *nres = nullptr;
    if (TC.isToFPRT()) {
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
      if (I.getReturnValue()->getType()->getScalarType() != getFromType())
        return;
      auto newI = cast<llvm::ReturnInst>(getNewFromOriginal(&I));
      IRBuilder<> B(newI);
      if (isa<ConstantFP>(newI->getOperand(0)) || 
          isVecOfConst(newI->getOperand(0)))
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
      if (orig_val->getType()->getScalarType() != getFromType())
        return;
      if (!isa<ConstantFP>(orig_val) && !isVecOfConst(orig_val))
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
    if (auto F = dyn_cast<Function>(v)) {
      auto NewTC = TC;
      NewTC.NeedNewScratch = !WillPassScratch;
      NewTC.NeedTruncChange = false;
      NewTC.ScratchFromArgs = WillPassScratch;
      return Logic.CreateTruncateFunc(ctx, F, NewTC);
    }
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
        llvm::Use * u;
        if (FTT.isCallbackFunc()) {
          newCall->setArgOperand(FTT.getCallbackArgNo(), val);
          u = CI.arg_begin() + FTT.getCallbackArgNo();
        } else {
          newCall->setCalledOperand(val);
          u = &CI.getCalledOperandUse();
        }
        llvm::AbstractCallSite ACS(u);
        if (Mode == TruncMemMode){
          for (unsigned i = 0; i < ACS.getNumArgOperands(); ++i) {
            auto arg = ACS.getCallArgOperand(i);
            if (arg && arg->getType()->getScalarType() == getFromType() &&
               (isa<ConstantFP>(arg) || isVecOfConst(arg))
            ) {
              newCall->setArgOperand(ACS.getCallArgOperandNo(i), 
                truncate(BuilderZ, getNewFromOriginal(arg)));
            }
          }
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
      if (PN.getType()->getScalarType() != getFromType())
        return;
      auto NewPN = cast<llvm::PHINode>(getNewFromOriginal(&PN));
      IRBuilder<> B(&*NewPN->getParent()
                          ->getParent()
                          ->getEntryBlock()
                          .getFirstNonPHIIt());
      for (unsigned It = 0; It < NewPN->getNumIncomingValues(); It++) {
        if (isa<ConstantFP>(NewPN->getIncomingValue(It)) || 
            isVecOfConst(NewPN->getIncomingValue(It))) {
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
  TruncateUtils TU(
      TruncationConfiguration::getInitial(Truncation, v->getContext()),
      B.GetInsertBlock()->getParent()->getParent(), *this);
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
