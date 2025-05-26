//===- RaptorClang.cpp - Automatic Differentiation Transformation Pass ----===//
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
// This file contains a clang plugin for Raptor.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"


#include "bundled_includes.h"

using namespace clang;

constexpr auto StructKind = clang::TagTypeKind::Struct;

template <typename ConsumerType>
class RaptorAction final : public clang::PluginASTAction {
protected:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI,
                    llvm::StringRef InFile) override {
    return std::unique_ptr<clang::ASTConsumer>(new ConsumerType(CI));
  }

  bool ParseArgs(const clang::CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return AddBeforeMainAction;
  }
};

void MakeGlobalOfFn(FunctionDecl *FD, CompilerInstance &CI) {
  // if (FD->isLateTemplateParsed()) return;
  // TODO save any type info into string like attribute
}

struct Visitor : public RecursiveASTVisitor<Visitor> {
  CompilerInstance &CI;
  Visitor(CompilerInstance &CI) : CI(CI) {}
  bool VisitFunctionDecl(FunctionDecl *FD) {
    MakeGlobalOfFn(FD, CI);
    return true;
  }
};

void registerRaptor(llvm::PassBuilder &PB);

class RaptorPlugin final : public clang::ASTConsumer {
  clang::CompilerInstance &CI;

public:
  RaptorPlugin(clang::CompilerInstance &CI) : CI(CI) {

    FrontendOptions &Opts = CI.getFrontendOpts();
    CodeGenOptions &CGOpts = CI.getCodeGenOpts();
    auto PluginName = "ClangRaptor-" + std::to_string(LLVM_VERSION_MAJOR);
    bool contains = false;
    for (auto P : Opts.Plugins)
      if (llvm::sys::path::stem(P).ends_with(PluginName)) {
        for (auto passPlugin : CGOpts.PassPlugins) {
          if (llvm::sys::path::stem(passPlugin).ends_with(PluginName)) {
            contains = true;
            break;
          }
        }
      }

    if (!contains) {
      CGOpts.PassBuilderCallbacks.push_back(registerRaptor);
    }
    CI.getPreprocessorOpts().Includes.push_back("/raptor/raptor/version");

    std::string PredefineBuffer;
    PredefineBuffer.reserve(4080);
    llvm::raw_string_ostream Predefines(PredefineBuffer);
    Predefines << CI.getPreprocessor().getPredefines();
    MacroBuilder Builder(Predefines);
    Builder.defineMacro("RAPTOR_VERSION_MAJOR",
                        std::to_string(RAPTOR_VERSION_MAJOR));
    Builder.defineMacro("RAPTOR_VERSION_MINOR",
                        std::to_string(RAPTOR_VERSION_MINOR));
    Builder.defineMacro("RAPTOR_VERSION_PATCH",
                        std::to_string(RAPTOR_VERSION_PATCH));
    CI.getPreprocessor().setPredefines(Predefines.str());

    auto baseFS = &CI.getFileManager().getVirtualFileSystem();
    llvm::vfs::OverlayFileSystem *fuseFS(
        new llvm::vfs::OverlayFileSystem(baseFS));
    IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> fs(
        new llvm::vfs::InMemoryFileSystem());

    struct tm y2k = {};

    y2k.tm_hour = 0;
    y2k.tm_min = 0;
    y2k.tm_sec = 0;
    y2k.tm_year = 100;
    y2k.tm_mon = 0;
    y2k.tm_mday = 1;
    time_t timer = mktime(&y2k);
    for (const auto &pair : include_headers) {
      fs->addFile(StringRef(pair[0]), timer,
                  llvm::MemoryBuffer::getMemBuffer(
                      StringRef(pair[1]), StringRef(pair[0]),
                      /*RequiresNullTerminator*/ true));
    }

    fuseFS->pushOverlay(fs);
    fuseFS->pushOverlay(baseFS);
    CI.getFileManager().setVirtualFileSystem(fuseFS);

    auto DE = CI.getFileManager().getDirectoryRef("/raptorroot");
    assert(DE);
    auto DL = DirectoryLookup(*DE, SrcMgr::C_User,
                              /*isFramework=*/false);
    CI.getPreprocessor().getHeaderSearchInfo().AddSearchPath(DL,
                                                             /*isAngled=*/true);
  }
  ~RaptorPlugin() {}
  void HandleTranslationUnit(ASTContext &context) override {}
  bool HandleTopLevelDecl(clang::DeclGroupRef dg) override {
    using namespace clang;
    DeclGroupRef::iterator it;

    // Visitor v(CI);
    // Forcibly require emission of all libdevice
    for (it = dg.begin(); it != dg.end(); ++it) {
      // v.TraverseDecl(*it);
      if (auto FD = dyn_cast<FunctionDecl>(*it)) {
        if (!FD->hasAttr<clang::CUDADeviceAttr>())
          continue;

        if (!FD->getIdentifier())
          continue;
        if (!StringRef(FD->getLocation().printToString(CI.getSourceManager()))
                 .contains("/__clang_cuda_math.h"))
          continue;

        FD->addAttr(UsedAttr::CreateImplicit(CI.getASTContext()));
      }
      if (auto FD = dyn_cast<VarDecl>(*it)) {
        HandleCXXStaticMemberVarInstantiation(FD);
      }
    }
    return true;
  }
  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *V) override {
    if (!V->getIdentifier())
      return;
    auto name = V->getName();
    if (!(name.contains("__raptor_inactive_global") ||
          name.contains("__raptor_inactivefn") ||
          name.contains("__raptor_shouldrecompute") ||
          name.contains("__raptor_function_like") ||
          name.contains("__raptor_allocation_like") ||
          name.contains("__raptor_register_gradient") ||
          name.contains("__raptor_register_derivative") ||
          name.contains("__raptor_register_splitderivative")))
      return;

    V->addAttr(clang::UsedAttr::CreateImplicit(CI.getASTContext()));
    return;
  }
};

// register the PluginASTAction in the registry.
static clang::FrontendPluginRegistry::Add<RaptorAction<RaptorPlugin>>
    X("raptor", "Raptor Plugin");

#if LLVM_VERSION_MAJOR > 10
namespace {

struct RaptorFunctionLikeAttrInfo : public ParsedAttrInfo {
  RaptorFunctionLikeAttrInfo() {
    OptArgs = 1;
    // GNU-style __attribute__(("example")) and C++/C2x-style [[example]] and
    // [[plugin::example]] supported.
    static constexpr Spelling S[] = {
      {ParsedAttr::AS_GNU, "raptor_function_like"},
#if LLVM_VERSION_MAJOR > 17
      {ParsedAttr::AS_C23, "raptor_function_like"},
#else
      {ParsedAttr::AS_C2x, "raptor_function_like"},
#endif
      {ParsedAttr::AS_CXX11, "raptor_function_like"},
      {ParsedAttr::AS_CXX11, "raptor::function_like"}
    };
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    // This attribute appertains to functions only.
    if (!isa<FunctionDecl>(D)) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << "functions";
      return false;
    }
    return true;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    if (Attr.getNumArgs() != 1) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'raptor_function' attribute requires a single string argument");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    auto *Arg0 = Attr.getArgAsExpr(0);
    StringLiteral *Literal = dyn_cast<StringLiteral>(Arg0->IgnoreParenCasts());
    if (!Literal) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error, "first argument to 'raptor_function_like' "
                                    "attribute must be a string literal");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
#if LLVM_VERSION_MAJOR >= 12
    D->addAttr(AnnotateAttr::Create(
        S.Context, ("raptor_function_like=" + Literal->getString()).str(),
        nullptr, 0, Attr.getRange()));
    return AttributeApplied;
#else
    auto FD = cast<FunctionDecl>(D);
    // if (FD->isLateTemplateParsed()) return;
    auto &AST = S.getASTContext();
    DeclContext *declCtx = FD->getDeclContext();
    for (auto tmpCtx = declCtx; tmpCtx; tmpCtx = tmpCtx->getParent()) {
      if (tmpCtx->isRecord()) {
        declCtx = tmpCtx->getParent();
      }
    }
    auto loc = FD->getLocation();
    RecordDecl *RD;
    if (S.getLangOpts().CPlusPlus)
      RD = CXXRecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                                 nullptr); // rId);
    else
      RD = RecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                              nullptr); // rId);
    RD->setAnonymousStructOrUnion(true);
    RD->setImplicit();
    RD->startDefinition();
    auto Tinfo = nullptr;
    auto Tinfo0 = nullptr;
    auto FT = AST.getPointerType(FD->getType());
    auto CharTy = AST.getIntTypeForBitwidth(8, false);
    auto FD0 = FieldDecl::Create(AST, RD, loc, loc, /*Ud*/ nullptr, FT, Tinfo0,
                                 /*expr*/ nullptr, /*mutable*/ true,
                                 /*inclassinit*/ ICIS_NoInit);
    FD0->setAccess(AS_public);
    RD->addDecl(FD0);
    auto FD1 = FieldDecl::Create(
        AST, RD, loc, loc, /*Ud*/ nullptr, AST.getPointerType(CharTy), Tinfo0,
        /*expr*/ nullptr, /*mutable*/ true, /*inclassinit*/ ICIS_NoInit);
    FD1->setAccess(AS_public);
    RD->addDecl(FD1);
    RD->completeDefinition();
    assert(RD->getDefinition());
    auto &Id = AST.Idents.get("__raptor_function_like_autoreg_" +
                              FD->getNameAsString());
    auto T = AST.getRecordType(RD);
    auto V = VarDecl::Create(AST, declCtx, loc, loc, &Id, T, Tinfo, SC_None);
    V->setStorageClass(SC_PrivateExtern);
    V->addAttr(clang::UsedAttr::CreateImplicit(AST));
    TemplateArgumentListInfo *TemplateArgs = nullptr;
    auto DR = DeclRefExpr::Create(AST, NestedNameSpecifierLoc(), loc, FD, false,
                                  loc, FD->getType(), ExprValueKind::VK_LValue,
                                  FD, TemplateArgs);
    auto rval = ExprValueKind::VK_PRValue;
    StringRef cstr = Literal->getString();
    Expr *exprs[2] = {
        ImplicitCastExpr::Create(AST, FT, CastKind::CK_FunctionToPointerDecay,
                                 DR, nullptr, rval, FPOptionsOverride()),
        ImplicitCastExpr::Create(
            AST, AST.getPointerType(CharTy), CastKind::CK_ArrayToPointerDecay,
            StringLiteral::Create(
                AST, cstr, stringkind,
                /*Pascal*/ false,
                AST.getStringLiteralArrayType(CharTy, cstr.size()), loc),
            nullptr, rval, FPOptionsOverride())};
    auto IL = new (AST) InitListExpr(AST, loc, exprs, loc);
    V->setInit(IL);
    IL->setType(T);
    if (IL->isValueDependent()) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error, "use of attribute 'raptor_function_like' "
                                    "in a templated context not yet supported");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    S.MarkVariableReferenced(loc, V);
    S.getASTConsumer().HandleTopLevelDecl(DeclGroupRef(V));
    return AttributeApplied;
#endif
  }
};

static ParsedAttrInfoRegistry::Add<RaptorFunctionLikeAttrInfo>
    X3("raptor_function_like", "");

struct RaptorShouldRecomputeAttrInfo : public ParsedAttrInfo {
  RaptorShouldRecomputeAttrInfo() {
    OptArgs = 1;
    static constexpr Spelling S[] = {
      {ParsedAttr::AS_GNU, "raptor_shouldrecompute"},
#if LLVM_VERSION_MAJOR > 17
      {ParsedAttr::AS_C23, "raptor_shouldrecompute"},
#else
      {ParsedAttr::AS_C2x, "raptor_shouldrecompute"},
#endif
      {ParsedAttr::AS_CXX11, "raptor_shouldrecompute"},
      {ParsedAttr::AS_CXX11, "raptor::shouldrecompute"}
    };
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    // This attribute appertains to functions only.
    if (isa<FunctionDecl>(D))
      return true;
    if (auto VD = dyn_cast<VarDecl>(D)) {
      if (VD->hasGlobalStorage())
        return true;
    }
    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
        << Attr << "functions and globals";
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    if (Attr.getNumArgs() != 0) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'raptor_inactive' attribute requires zero arguments");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    D->addAttr(AnnotateAttr::Create(S.Context, "raptor_shouldrecompute",
                                    nullptr, 0, Attr.getRange()));
    return AttributeApplied;
  }
};

static ParsedAttrInfoRegistry::Add<RaptorShouldRecomputeAttrInfo>
    ESR("raptor_shouldrecompute", "");

struct RaptorInactiveAttrInfo : public ParsedAttrInfo {
  RaptorInactiveAttrInfo() {
    OptArgs = 1;
    // GNU-style __attribute__(("example")) and C++/C2x-style [[example]] and
    // [[plugin::example]] supported.
    static constexpr Spelling S[] = {
      {ParsedAttr::AS_GNU, "raptor_inactive"},
#if LLVM_VERSION_MAJOR > 17
      {ParsedAttr::AS_C23, "raptor_inactive"},
#else
      {ParsedAttr::AS_C2x, "raptor_inactive"},
#endif
      {ParsedAttr::AS_CXX11, "raptor_inactive"},
      {ParsedAttr::AS_CXX11, "raptor::inactive"}
    };
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    // This attribute appertains to functions only.
    if (isa<FunctionDecl>(D))
      return true;
    if (auto VD = dyn_cast<VarDecl>(D)) {
      if (VD->hasGlobalStorage())
        return true;
    }
    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
        << Attr << "functions and globals";
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    if (Attr.getNumArgs() != 0) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'raptor_inactive' attribute requires zero arguments");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }

    auto &AST = S.getASTContext();
    DeclContext *declCtx = D->getDeclContext();
    for (auto tmpCtx = declCtx; tmpCtx; tmpCtx = tmpCtx->getParent()) {
      if (tmpCtx->isRecord()) {
        declCtx = tmpCtx->getParent();
      }
    }
    auto loc = D->getLocation();
    RecordDecl *RD;
    if (S.getLangOpts().CPlusPlus)
      RD = CXXRecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                                 nullptr); // rId);
    else
      RD = RecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                              nullptr); // rId);
    RD->setAnonymousStructOrUnion(true);
    RD->setImplicit();
    RD->startDefinition();
    auto T = isa<FunctionDecl>(D) ? cast<FunctionDecl>(D)->getType()
                                  : cast<VarDecl>(D)->getType();
    auto Name = isa<FunctionDecl>(D) ? cast<FunctionDecl>(D)->getNameAsString()
                                     : cast<VarDecl>(D)->getNameAsString();
    auto FT = AST.getPointerType(T);
    auto subname = isa<FunctionDecl>(D) ? "inactivefn" : "inactive_global";
    auto &Id = AST.Idents.get(
        (StringRef("__raptor_") + subname + "_autoreg_" + Name).str());
    auto V = VarDecl::Create(AST, declCtx, loc, loc, &Id, FT, nullptr, SC_None);
    V->setStorageClass(SC_PrivateExtern);
    V->addAttr(clang::UsedAttr::CreateImplicit(AST));
    TemplateArgumentListInfo *TemplateArgs = nullptr;
    auto DR = DeclRefExpr::Create(
        AST, NestedNameSpecifierLoc(), loc, cast<ValueDecl>(D), false, loc, T,
        ExprValueKind::VK_LValue, cast<NamedDecl>(D), TemplateArgs);
    auto rval = ExprValueKind::VK_PRValue;
    Expr *expr = nullptr;
    if (isa<FunctionDecl>(D)) {
      expr =
          ImplicitCastExpr::Create(AST, FT, CastKind::CK_FunctionToPointerDecay,
                                   DR, nullptr, rval, FPOptionsOverride());
    } else {
      expr =
          UnaryOperator::Create(AST, DR, UnaryOperatorKind::UO_AddrOf, FT, rval,
                                clang::ExprObjectKind ::OK_Ordinary, loc,
                                /*canoverflow*/ false, FPOptionsOverride());
    }

    if (expr->isValueDependent()) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error, "use of attribute 'raptor_inactive' "
                                    "in a templated context not yet supported");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    V->setInit(expr);
    S.MarkVariableReferenced(loc, V);
    S.getASTConsumer().HandleTopLevelDecl(DeclGroupRef(V));
    return AttributeApplied;
  }
};

static ParsedAttrInfoRegistry::Add<RaptorInactiveAttrInfo> X4("raptor_inactive",
                                                              "");

struct RaptorNoFreeAttrInfo : public ParsedAttrInfo {
  RaptorNoFreeAttrInfo() {
    OptArgs = 1;
    // GNU-style __attribute__(("example")) and C++/C2x-style [[example]] and
    // [[plugin::example]] supported.
    static constexpr Spelling S[] = {
      {ParsedAttr::AS_GNU, "raptor_nofree"},
#if LLVM_VERSION_MAJOR > 17
      {ParsedAttr::AS_C23, "raptor_nofree"},
#else
      {ParsedAttr::AS_C2x, "raptor_nofree"},
#endif
      {ParsedAttr::AS_CXX11, "raptor_nofree"},
      {ParsedAttr::AS_CXX11, "raptor::nofree"}
    };
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    // This attribute appertains to functions only.
    if (isa<FunctionDecl>(D))
      return true;
    if (auto VD = dyn_cast<VarDecl>(D)) {
      if (VD->hasGlobalStorage())
        return true;
    }
    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
        << Attr << "functions and globals";
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    if (Attr.getNumArgs() != 0) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'raptor_nofree' attribute requires zero arguments");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }

    auto &AST = S.getASTContext();
    DeclContext *declCtx = D->getDeclContext();
    for (auto tmpCtx = declCtx; tmpCtx; tmpCtx = tmpCtx->getParent()) {
      if (tmpCtx->isRecord()) {
        declCtx = tmpCtx->getParent();
      }
    }
    auto loc = D->getLocation();
    RecordDecl *RD;
    if (S.getLangOpts().CPlusPlus)
      RD = CXXRecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                                 nullptr); // rId);
    else
      RD = RecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                              nullptr); // rId);
    RD->setAnonymousStructOrUnion(true);
    RD->setImplicit();
    RD->startDefinition();
    auto T = isa<FunctionDecl>(D) ? cast<FunctionDecl>(D)->getType()
                                  : cast<VarDecl>(D)->getType();
    auto Name = isa<FunctionDecl>(D) ? cast<FunctionDecl>(D)->getNameAsString()
                                     : cast<VarDecl>(D)->getNameAsString();
    auto FT = AST.getPointerType(T);
    auto &Id = AST.Idents.get(
        (StringRef("__raptor_nofree") + "_autoreg_" + Name).str());
    auto V = VarDecl::Create(AST, declCtx, loc, loc, &Id, FT, nullptr, SC_None);
    V->setStorageClass(SC_PrivateExtern);
    V->addAttr(clang::UsedAttr::CreateImplicit(AST));
    TemplateArgumentListInfo *TemplateArgs = nullptr;
    auto DR = DeclRefExpr::Create(
        AST, NestedNameSpecifierLoc(), loc, cast<ValueDecl>(D), false, loc, T,
        ExprValueKind::VK_LValue, cast<NamedDecl>(D), TemplateArgs);
    auto rval = ExprValueKind::VK_PRValue;
    Expr *expr = nullptr;
    if (isa<FunctionDecl>(D)) {
      expr =
          ImplicitCastExpr::Create(AST, FT, CastKind::CK_FunctionToPointerDecay,
                                   DR, nullptr, rval, FPOptionsOverride());
    } else {
      expr =
          UnaryOperator::Create(AST, DR, UnaryOperatorKind::UO_AddrOf, FT, rval,
                                clang::ExprObjectKind ::OK_Ordinary, loc,
                                /*canoverflow*/ false, FPOptionsOverride());
    }

    if (expr->isValueDependent()) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error, "use of attribute 'raptor_nofree' "
                                    "in a templated context not yet supported");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    V->setInit(expr);
    S.MarkVariableReferenced(loc, V);
    S.getASTConsumer().HandleTopLevelDecl(DeclGroupRef(V));
    return AttributeApplied;
  }
};

static ParsedAttrInfoRegistry::Add<RaptorNoFreeAttrInfo> X5("raptor_nofree",
                                                            "");

struct RaptorSparseAccumulateAttrInfo : public ParsedAttrInfo {
  RaptorSparseAccumulateAttrInfo() {
    OptArgs = 1;
    // GNU-style __attribute__(("example")) and C++/C2x-style [[example]] and
    // [[plugin::example]] supported.
    static constexpr Spelling S[] = {
      {ParsedAttr::AS_GNU, "raptor_sparse_accumulate"},
#if LLVM_VERSION_MAJOR > 17
      {ParsedAttr::AS_C23, "raptor_sparse_accumulate"},
#else
      {ParsedAttr::AS_C2x, "raptor_sparse_accumulate"},
#endif
      {ParsedAttr::AS_CXX11, "raptor_sparse_accumulate"},
      {ParsedAttr::AS_CXX11, "raptor::sparse_accumulate"}
    };
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    // This attribute appertains to functions only.
    if (isa<FunctionDecl>(D))
      return true;
    S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
        << Attr << "functions";
    return false;
  }

  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    if (Attr.getNumArgs() != 0) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "'raptor_sparse_accumulate' attribute requires zero arguments");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }

    auto &AST = S.getASTContext();
    DeclContext *declCtx = D->getDeclContext();
    for (auto tmpCtx = declCtx; tmpCtx; tmpCtx = tmpCtx->getParent()) {
      if (tmpCtx->isRecord()) {
        declCtx = tmpCtx->getParent();
      }
    }
    auto loc = D->getLocation();
    RecordDecl *RD;
    if (S.getLangOpts().CPlusPlus)
      RD = CXXRecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                                 nullptr); // rId);
    else
      RD = RecordDecl::Create(AST, StructKind, declCtx, loc, loc,
                              nullptr); // rId);
    RD->setAnonymousStructOrUnion(true);
    RD->setImplicit();
    RD->startDefinition();
    auto T = cast<FunctionDecl>(D)->getType();
    auto Name = cast<FunctionDecl>(D)->getNameAsString();
    auto FT = AST.getPointerType(T);
    auto &Id = AST.Idents.get(
        (StringRef("__raptor_sparse_accumulate") + "_autoreg_" + Name).str());
    auto V = VarDecl::Create(AST, declCtx, loc, loc, &Id, FT, nullptr, SC_None);
    V->setStorageClass(SC_PrivateExtern);
    V->addAttr(clang::UsedAttr::CreateImplicit(AST));
    TemplateArgumentListInfo *TemplateArgs = nullptr;
    auto DR = DeclRefExpr::Create(
        AST, NestedNameSpecifierLoc(), loc, cast<ValueDecl>(D), false, loc, T,
        ExprValueKind::VK_LValue, cast<NamedDecl>(D), TemplateArgs);
    auto rval = ExprValueKind::VK_PRValue;
    Expr *expr = nullptr;
    expr =
        ImplicitCastExpr::Create(AST, FT, CastKind::CK_FunctionToPointerDecay,
                                 DR, nullptr, rval, FPOptionsOverride());

    if (expr->isValueDependent()) {
      unsigned ID = S.getDiagnostics().getCustomDiagID(
          DiagnosticsEngine::Error,
          "use of attribute 'raptor_sparse_accumulate' "
          "in a templated context not yet supported");
      S.Diag(Attr.getLoc(), ID);
      return AttributeNotApplied;
    }
    V->setInit(expr);
    S.MarkVariableReferenced(loc, V);
    S.getASTConsumer().HandleTopLevelDecl(DeclGroupRef(V));
    return AttributeApplied;
  }
};

static ParsedAttrInfoRegistry::Add<RaptorSparseAccumulateAttrInfo>
    SparseX("raptor_sparse_accumulate", "");
} // namespace

#endif
