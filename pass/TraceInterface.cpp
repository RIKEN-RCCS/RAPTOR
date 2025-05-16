//===- TraceInterface.h - Interact with probabilistic programming traces
//---===//
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
// This file contains an abstraction for static and dynamic implementations of
// the probabilistic programming interface.
//
//===----------------------------------------------------------------------===//

#include "TraceInterface.h"


#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

using namespace llvm;

TraceInterface::TraceInterface(LLVMContext &C) : C(C){};

PointerType *traceType(LLVMContext &C) {
  return getDefaultAnonymousTapeType(C);
}

Type *addressType(LLVMContext &C) { return getInt8PtrTy(C); }

IntegerType *TraceInterface::sizeType(LLVMContext &C) {
  return IntegerType::getInt64Ty(C);
}

Type *TraceInterface::stringType(LLVMContext &C) { return getInt8PtrTy(C); }

FunctionType *TraceInterface::getTraceTy() { return getTraceTy(C); }
FunctionType *TraceInterface::getChoiceTy() { return getChoiceTy(C); }
FunctionType *TraceInterface::insertCallTy() { return insertCallTy(C); }
FunctionType *TraceInterface::insertChoiceTy() { return insertChoiceTy(C); }
FunctionType *TraceInterface::insertArgumentTy() { return insertArgumentTy(C); }
FunctionType *TraceInterface::insertReturnTy() { return insertReturnTy(C); }
FunctionType *TraceInterface::insertFunctionTy() { return insertFunctionTy(C); }
FunctionType *TraceInterface::insertChoiceGradientTy() {
  return insertChoiceGradientTy(C);
}
FunctionType *TraceInterface::insertArgumentGradientTy() {
  return insertArgumentGradientTy(C);
}
FunctionType *TraceInterface::newTraceTy() { return newTraceTy(C); }
FunctionType *TraceInterface::freeTraceTy() { return freeTraceTy(C); }
FunctionType *TraceInterface::hasCallTy() { return hasCallTy(C); }
FunctionType *TraceInterface::hasChoiceTy() { return hasChoiceTy(C); }

FunctionType *TraceInterface::getTraceTy(LLVMContext &C) {
  return FunctionType::get(traceType(C), {traceType(C), stringType(C)}, false);
}

FunctionType *TraceInterface::getChoiceTy(LLVMContext &C) {
  return FunctionType::get(
      sizeType(C), {traceType(C), stringType(C), addressType(C), sizeType(C)},
      false);
}

FunctionType *TraceInterface::insertCallTy(LLVMContext &C) {
  return FunctionType::get(Type::getVoidTy(C),
                           {getInt8PtrTy(C), stringType(C), getInt8PtrTy(C)},
                           false);
}

FunctionType *TraceInterface::insertChoiceTy(LLVMContext &C) {
  return FunctionType::get(Type::getVoidTy(C),
                           {getInt8PtrTy(C), stringType(C),
                            Type::getDoubleTy(C), getInt8PtrTy(C), sizeType(C)},
                           false);
}

FunctionType *TraceInterface::insertArgumentTy(LLVMContext &C) {
  return FunctionType::get(
      Type::getVoidTy(C),
      {getInt8PtrTy(C), stringType(C), getInt8PtrTy(C), sizeType(C)}, false);
}

FunctionType *TraceInterface::insertReturnTy(LLVMContext &C) {
  return FunctionType::get(Type::getVoidTy(C),
                           {getInt8PtrTy(C), getInt8PtrTy(C), sizeType(C)},
                           false);
}

FunctionType *TraceInterface::insertFunctionTy(LLVMContext &C) {
  return FunctionType::get(Type::getVoidTy(C),
                           {getInt8PtrTy(C), getInt8PtrTy(C)}, false);
}

FunctionType *TraceInterface::insertChoiceGradientTy(LLVMContext &C) {
  return FunctionType::get(
      Type::getVoidTy(C),
      {getInt8PtrTy(C), stringType(C), getInt8PtrTy(C), sizeType(C)}, false);
}

FunctionType *TraceInterface::insertArgumentGradientTy(LLVMContext &C) {
  return FunctionType::get(
      Type::getVoidTy(C),
      {getInt8PtrTy(C), stringType(C), getInt8PtrTy(C), sizeType(C)}, false);
}

FunctionType *TraceInterface::newTraceTy(LLVMContext &C) {
  return FunctionType::get(getInt8PtrTy(C), {}, false);
}

FunctionType *TraceInterface::freeTraceTy(LLVMContext &C) {
  return FunctionType::get(Type::getVoidTy(C), {getInt8PtrTy(C)}, false);
}

FunctionType *TraceInterface::hasCallTy(LLVMContext &C) {
  return FunctionType::get(Type::getInt1Ty(C), {getInt8PtrTy(C), stringType(C)},
                           false);
}

FunctionType *TraceInterface::hasChoiceTy(LLVMContext &C) {
  return FunctionType::get(Type::getInt1Ty(C), {getInt8PtrTy(C), stringType(C)},
                           false);
}

StaticTraceInterface::StaticTraceInterface(Module *M)
    : TraceInterface(M->getContext()) {
  for (auto &&F : M->functions()) {
    if (F.isIntrinsic())
      continue;
    if (F.getName().contains("__raptor_newtrace")) {
      assert(F.getFunctionType() == newTraceTy());
      newTraceFunction = &F;
    } else if (F.getName().contains("__raptor_freetrace")) {
      assert(F.getFunctionType() == freeTraceTy());
      freeTraceFunction = &F;
    } else if (F.getName().contains("__raptor_get_trace")) {
      assert(F.getFunctionType() == getTraceTy());
      getTraceFunction = &F;
    } else if (F.getName().contains("__raptor_get_choice")) {
      assert(F.getFunctionType() == getChoiceTy());
      getChoiceFunction = &F;
    } else if (F.getName().contains("__raptor_insert_call")) {
      assert(F.getFunctionType() == insertCallTy());
      insertCallFunction = &F;
    } else if (F.getName().contains("__raptor_insert_choice")) {
      assert(F.getFunctionType() == insertChoiceTy());
      insertChoiceFunction = &F;
    } else if (F.getName().contains("__raptor_insert_argument")) {
      assert(F.getFunctionType() == insertArgumentTy());
      insertArgumentFunction = &F;
    } else if (F.getName().contains("__raptor_insert_return")) {
      assert(F.getFunctionType() == insertReturnTy());
      insertReturnFunction = &F;
    } else if (F.getName().contains("__raptor_insert_function")) {
      assert(F.getFunctionType() == insertFunctionTy());
      insertFunctionFunction = &F;
    } else if (F.getName().contains("__raptor_insert_gradient_choice")) {
      assert(F.getFunctionType() == insertChoiceGradientTy());
      insertChoiceGradientFunction = &F;
    } else if (F.getName().contains("__raptor_insert_gradient_argument")) {
      assert(F.getFunctionType() == insertArgumentGradientTy());
      insertArgumentGradientFunction = &F;
    } else if (F.getName().contains("__raptor_has_call")) {
      assert(F.getFunctionType() == hasCallTy());
      hasCallFunction = &F;
    } else if (F.getName().contains("__raptor_has_choice")) {
      assert(F.getFunctionType() == hasChoiceTy());
      hasChoiceFunction = &F;
    }
  }

  assert(newTraceFunction);
  assert(freeTraceFunction);
  assert(getTraceFunction);
  assert(getChoiceFunction);
  assert(insertCallFunction);
  assert(insertChoiceFunction);

  assert(insertArgumentFunction);
  assert(insertReturnFunction);
  assert(insertFunctionFunction);

  assert(insertChoiceGradientFunction);
  assert(insertArgumentGradientFunction);

  assert(hasCallFunction);
  assert(hasChoiceFunction);

  newTraceFunction->addFnAttr("raptor_notypeanalysis");
  freeTraceFunction->addFnAttr("raptor_notypeanalysis");
  getTraceFunction->addFnAttr("raptor_notypeanalysis");
  getChoiceFunction->addFnAttr("raptor_notypeanalysis");
  insertCallFunction->addFnAttr("raptor_notypeanalysis");
  insertChoiceFunction->addFnAttr("raptor_notypeanalysis");
  insertArgumentFunction->addFnAttr("raptor_notypeanalysis");
  insertReturnFunction->addFnAttr("raptor_notypeanalysis");
  insertFunctionFunction->addFnAttr("raptor_notypeanalysis");
  insertChoiceGradientFunction->addFnAttr("raptor_notypeanalysis");
  insertArgumentGradientFunction->addFnAttr("raptor_notypeanalysis");
  hasCallFunction->addFnAttr("raptor_notypeanalysis");
  hasChoiceFunction->addFnAttr("raptor_notypeanalysis");

  newTraceFunction->addFnAttr("raptor_inactive");
  freeTraceFunction->addFnAttr("raptor_inactive");
  getTraceFunction->addFnAttr("raptor_inactive");
  getChoiceFunction->addFnAttr("raptor_inactive");
  insertCallFunction->addFnAttr("raptor_inactive");
  insertChoiceFunction->addFnAttr("raptor_inactive");
  insertArgumentFunction->addFnAttr("raptor_inactive");
  insertReturnFunction->addFnAttr("raptor_inactive");
  insertFunctionFunction->addFnAttr("raptor_inactive");
  insertChoiceGradientFunction->addFnAttr("raptor_inactive");
  insertArgumentGradientFunction->addFnAttr("raptor_inactive");
  hasCallFunction->addFnAttr("raptor_inactive");
  hasChoiceFunction->addFnAttr("raptor_inactive");

  newTraceFunction->addFnAttr(Attribute::NoFree);
  getTraceFunction->addFnAttr(Attribute::NoFree);
  getChoiceFunction->addFnAttr(Attribute::NoFree);
  insertCallFunction->addFnAttr(Attribute::NoFree);
  insertChoiceFunction->addFnAttr(Attribute::NoFree);
  insertArgumentFunction->addFnAttr(Attribute::NoFree);
  insertReturnFunction->addFnAttr(Attribute::NoFree);
  insertFunctionFunction->addFnAttr(Attribute::NoFree);
  insertChoiceGradientFunction->addFnAttr(Attribute::NoFree);
  insertArgumentGradientFunction->addFnAttr(Attribute::NoFree);
  hasCallFunction->addFnAttr(Attribute::NoFree);
  hasChoiceFunction->addFnAttr(Attribute::NoFree);
}

StaticTraceInterface::StaticTraceInterface(
    LLVMContext &C, Function *getTraceFunction, Function *getChoiceFunction,
    Function *insertCallFunction, Function *insertChoiceFunction,
    Function *insertArgumentFunction, Function *insertReturnFunction,
    Function *insertFunctionFunction, Function *insertChoiceGradientFunction,
    Function *insertArgumentGradientFunction, Function *newTraceFunction,
    Function *freeTraceFunction, Function *hasCallFunction,
    Function *hasChoiceFunction)
    : TraceInterface(C), getTraceFunction(getTraceFunction),
      getChoiceFunction(getChoiceFunction),
      insertCallFunction(insertCallFunction),
      insertChoiceFunction(insertChoiceFunction),
      insertArgumentFunction(insertArgumentFunction),
      insertReturnFunction(insertReturnFunction),
      insertFunctionFunction(insertFunctionFunction),
      insertChoiceGradientFunction(insertChoiceGradientFunction),
      insertArgumentGradientFunction(insertArgumentGradientFunction),
      newTraceFunction(newTraceFunction), freeTraceFunction(freeTraceFunction),
      hasCallFunction(hasCallFunction), hasChoiceFunction(hasChoiceFunction){};

// user implemented
Value *StaticTraceInterface::getTrace(IRBuilder<> &Builder) {
  return getTraceFunction;
}
Value *StaticTraceInterface::getChoice(IRBuilder<> &Builder) {
  return getChoiceFunction;
}
Value *StaticTraceInterface::insertCall(IRBuilder<> &Builder) {
  return insertCallFunction;
}
Value *StaticTraceInterface::insertChoice(IRBuilder<> &Builder) {
  return insertChoiceFunction;
}
Value *StaticTraceInterface::insertArgument(IRBuilder<> &Builder) {
  return insertArgumentFunction;
}
Value *StaticTraceInterface::insertReturn(IRBuilder<> &Builder) {
  return insertReturnFunction;
}
Value *StaticTraceInterface::insertFunction(IRBuilder<> &Builder) {
  return insertFunctionFunction;
}
Value *StaticTraceInterface::insertChoiceGradient(IRBuilder<> &Builder) {
  return insertChoiceGradientFunction;
}
Value *StaticTraceInterface::insertArgumentGradient(IRBuilder<> &Builder) {
  return insertArgumentGradientFunction;
}
Value *StaticTraceInterface::newTrace(IRBuilder<> &Builder) {
  return newTraceFunction;
}
Value *StaticTraceInterface::freeTrace(IRBuilder<> &Builder) {
  return freeTraceFunction;
}
Value *StaticTraceInterface::hasCall(IRBuilder<> &Builder) {
  return hasCallFunction;
}
Value *StaticTraceInterface::hasChoice(IRBuilder<> &Builder) {
  return hasChoiceFunction;
}

DynamicTraceInterface::DynamicTraceInterface(Value *dynamicInterface,
                                             Function *F)
    : TraceInterface(F->getContext()) {
  assert(dynamicInterface);

  auto &M = *F->getParent();
  IRBuilder<> Builder(F->getEntryBlock().getFirstNonPHIOrDbg());

  getTraceFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, getTraceTy(), 0, M, "get_trace");
  getChoiceFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, getChoiceTy(), 1, M, "get_choice");
  insertCallFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertCallTy(), 2, M, "insert_call");
  insertChoiceFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertChoiceTy(), 3, M, "insert_choice");
  insertArgumentFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertArgumentTy(), 4, M, "insert_argument");
  insertReturnFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertReturnTy(), 5, M, "insert_return");
  insertFunctionFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertFunctionTy(), 6, M, "insert_function");
  insertChoiceGradientFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertChoiceGradientTy(), 7, M,
      "insert_choice_gradient");
  insertArgumentGradientFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, insertArgumentGradientTy(), 8, M,
      "insert_argument_gradient");
  newTraceFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, newTraceTy(), 9, M, "new_trace");
  freeTraceFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, freeTraceTy(), 10, M, "free_trace");
  hasCallFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, hasCallTy(), 11, M, "has_call");
  hasChoiceFunction = MaterializeInterfaceFunction(
      Builder, dynamicInterface, hasChoiceTy(), 12, M, "has_choice");

  assert(newTraceFunction);
  assert(freeTraceFunction);
  assert(getTraceFunction);
  assert(getChoiceFunction);
  assert(insertCallFunction);
  assert(insertChoiceFunction);

  assert(insertArgumentFunction);
  assert(insertReturnFunction);
  assert(insertFunctionFunction);

  assert(insertChoiceGradientFunction);
  assert(insertArgumentGradientFunction);

  assert(hasCallFunction);
  assert(hasChoiceFunction);
}

Function *DynamicTraceInterface::MaterializeInterfaceFunction(
    IRBuilder<> &Builder, Value *dynamicInterface, FunctionType *FTy,
    unsigned index, Module &M, const Twine &Name) {
  auto ptr =
      Builder.CreateInBoundsGEP(getInt8PtrTy(dynamicInterface->getContext()),
                                dynamicInterface, Builder.getInt32(index));
  auto load =
      Builder.CreateLoad(getInt8PtrTy(dynamicInterface->getContext()), ptr);
  auto pty = PointerType::get(FTy, load->getPointerAddressSpace());
  auto cast = Builder.CreatePointerCast(load, pty);

  auto global =
      new GlobalVariable(M, pty, false, GlobalVariable::PrivateLinkage,
                         ConstantPointerNull::get(pty), Name + "_ptr");
  Builder.CreateStore(cast, global);

  Function *F = Function::Create(FTy, Function::PrivateLinkage, Name, M);
  F->addFnAttr(Attribute::AlwaysInline);
  BasicBlock *Entry = BasicBlock::Create(M.getContext(), "entry", F);

  IRBuilder<> WrapperBuilder(Entry);

  auto ToWrap = WrapperBuilder.CreateLoad(pty, global, Name);
  auto Args = SmallVector<Value *, 4>(make_pointer_range(F->args()));
  auto Call = WrapperBuilder.CreateCall(FTy, ToWrap, Args);

  if (!FTy->getReturnType()->isVoidTy()) {
    WrapperBuilder.CreateRet(Call);
  } else {
    WrapperBuilder.CreateRetVoid();
  }

  return F;
}

// user implemented
Value *DynamicTraceInterface::getTrace(IRBuilder<> &Builder) {
  return getTraceFunction;
}

Value *DynamicTraceInterface::getChoice(IRBuilder<> &Builder) {
  return getChoiceFunction;
}

Value *DynamicTraceInterface::insertCall(IRBuilder<> &Builder) {
  return insertCallFunction;
}

Value *DynamicTraceInterface::insertChoice(IRBuilder<> &Builder) {
  return insertChoiceFunction;
}

Value *DynamicTraceInterface::insertArgument(IRBuilder<> &Builder) {
  return insertArgumentFunction;
}

Value *DynamicTraceInterface::insertReturn(IRBuilder<> &Builder) {
  return insertReturnFunction;
}

Value *DynamicTraceInterface::insertFunction(IRBuilder<> &Builder) {
  return insertFunctionFunction;
}

Value *DynamicTraceInterface::insertChoiceGradient(IRBuilder<> &Builder) {
  return insertChoiceGradientFunction;
}

Value *DynamicTraceInterface::insertArgumentGradient(IRBuilder<> &Builder) {
  return insertArgumentGradientFunction;
}

Value *DynamicTraceInterface::newTrace(IRBuilder<> &Builder) {
  return newTraceFunction;
}

Value *DynamicTraceInterface::freeTrace(IRBuilder<> &Builder) {
  return freeTraceFunction;
}

Value *DynamicTraceInterface::hasCall(IRBuilder<> &Builder) {
  return hasCallFunction;
}

Value *DynamicTraceInterface::hasChoice(IRBuilder<> &Builder) {
  return hasChoiceFunction;
}
