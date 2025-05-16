//===- Raptor.h - Automatic Differentiation Transformation Pass    -------===//
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

#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"

llvm::ModulePass *createRaptorPass(bool PostOpt = false);
void augmentPassBuilder(llvm::PassBuilder &PB);
