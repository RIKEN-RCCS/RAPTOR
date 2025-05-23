//===- Utils.cpp - Definition of miscellaneous utilities ------------------===//
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

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include "Utils.h"

llvm::CallInst *createIntrinsicCall(llvm::IRBuilderBase &B,
                                    llvm::Intrinsic::ID ID, llvm::Type *RetTy,
                                    llvm::ArrayRef<llvm::Value *> Args,
                                    llvm::Instruction *FMFSource,
                                    const llvm::Twine &Name) {
#if LLVM_VERSION_MAJOR >= 16
  llvm::CallInst *nres = B.CreateIntrinsic(RetTy, ID, Args, FMFSource, Name);
#else
  SmallVector<Intrinsic::IITDescriptor, 1> Table;
  Intrinsic::getIntrinsicInfoTableEntries(ID, Table);
  ArrayRef<Intrinsic::IITDescriptor> TableRef(Table);

  SmallVector<Type *, 2> ArgTys;
  ArgTys.reserve(Args.size());
  for (auto &I : Args)
    ArgTys.push_back(I->getType());
  FunctionType *FTy = FunctionType::get(RetTy, ArgTys, false);
  SmallVector<Type *, 2> OverloadTys;
  Intrinsic::MatchIntrinsicTypesResult Res =
      matchIntrinsicSignature(FTy, TableRef, OverloadTys);
  (void)Res;
  assert(Res == Intrinsic::MatchIntrinsicTypes_Match && TableRef.empty() &&
         "Wrong types for intrinsic!");
  Function *Fn = Intrinsic::getDeclaration(B.GetInsertPoint()->getModule(), ID,
                                           OverloadTys);
  CallInst *nres = B.CreateCall(Fn, Args, {}, Name);
  if (FMFSource)
    nres->copyFastMathFlags(FMFSource);
#endif
  return nres;
}

RaptorFailure::RaptorFailure(const llvm::Twine &RemarkName,
                             const llvm::DiagnosticLocation &Loc,
                             const llvm::Instruction *CodeRegion)
    : RaptorFailure(RemarkName, Loc, CodeRegion->getParent()->getParent()) {}

RaptorFailure::RaptorFailure(const llvm::Twine &RemarkName,
                             const llvm::DiagnosticLocation &Loc,
                             const llvm::Function *CodeRegion)
    : DiagnosticInfoUnsupported(*CodeRegion, RemarkName, Loc) {}
