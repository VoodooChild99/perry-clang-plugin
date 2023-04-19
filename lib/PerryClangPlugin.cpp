#include "PerryClangPlugin.h"

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/MacroArgs.h"

#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/Compiler.h"

using namespace clang;
using namespace ast_matchers;

// PerryEnumMatcher
PerryEnumMatcher::PerryEnumMatcher(EnumMapTy &EnumValToDecl)
  : EnumValToDecl(EnumValToDecl) {}

void PerryEnumMatcher::run(const MatchFinder::MatchResult &Result) {
  const EnumDecl *EnumDef = Result.Nodes.getNodeAs<EnumDecl>("EnumDef");
  if (!EnumDef) {
    return;
  }

  for (const EnumConstantDecl *EnumVal : EnumDef->enumerators()) {
    if (EnumValToDecl.find(EnumVal) == EnumValToDecl.end()) {
      EnumValToDecl.insert(std::make_pair(EnumVal, EnumDef));
    }
  }
}

PerryLoopMatcher::
PerryLoopMatcher(SourceManager &SM, LoopRangeSet &Loops)
  : SM(SM), Loops(Loops) {}

void PerryLoopMatcher::run(const MatchFinder::MatchResult &Result) {
  const ForStmt *ForLoop = Result.Nodes.getNodeAs<ForStmt>("ForLoop");
  const WhileStmt *WhileLoop = Result.Nodes.getNodeAs<WhileStmt>("WhileLoop");
  if (ForLoop) {
    Loops.insert(std::make_pair(ForLoop->getForLoc().getRawEncoding(),
                                ForLoop->getRParenLoc().getRawEncoding()));
  }

  if (WhileLoop) {
    Loops.insert(std::make_pair(WhileLoop->getWhileLoc().getRawEncoding(),
                                WhileLoop->getRParenLoc().getRawEncoding()));
  }

}

// PerryVisitor implementation
bool PerryVisitor::isGoodEnumName(const llvm::StringRef &Name) {
  static const std::vector<llvm::StringRef> GoodNameElements {
    "ok", "success"
  };
  for (auto &GNE : GoodNameElements) {
    if (Name.contains_insensitive(GNE)) {
      return true;
    }
  }
  return false;
}

bool PerryVisitor::TraverseFunctionDecl(FunctionDecl *FD) {
  // do nothing when the function:
  //  a) does not return, or
  if (FD->isNoReturn()) {
    return true;
  }
  //  b) has no implementation body
  std::string FuncName = FD->getNameAsString();
  bool inMainFile = Context->getSourceManager()
                      .isInMainFile(FD->getSourceRange().getBegin());
  bool inSystemFile = Context->getSourceManager()
                      .isInSystemHeader(FD->getSourceRange().getBegin());
  if (!FD->hasBody()) {
    if (!inMainFile && !inSystemFile) {
      FuncDec.insert(FuncName);
    }
    return true;
  }

  // the function has a body
  if (inMainFile) {
    FuncDef.insert(FuncName);
  } else if (!inSystemFile) {
    FuncDec.insert(FuncName);
  }

  // have we analyzed this function?
  if (SuccRetValMap.find(FuncName) != SuccRetValMap.end()) {
    return true;
  }

  QualType RetType = FD->getDeclaredReturnType();
  // if the function returns an enum according to signature, take the fast path
  if (RetType->isEnumeralType()) {
    const EnumType *RetEnumType = cast<EnumType>(RetType.getCanonicalType());
    for (auto EnumVal : RetEnumType->getDecl()->enumerators()) {
      // indicating success
      if (isGoodEnumName(EnumVal->getName())) {
        SuccRetValMap.insert(
          std::make_pair(FuncName,EnumVal->getInitVal().getZExtValue()));
        break;
      }
    }
    return true;
  }
  // else, take the slow path
  // clear placeholders
  retEnum.clear();
  retVar.clear();
  varDeclWithEnum.clear();
  varStoredWithEnum.clear();
  // traverse function body, visitors will be invoked along the way
  auto Ret = RecursiveASTVisitor::TraverseStmt(FD->getBody());
  // process data
  if (!retEnum.empty()) {
    // returns an enum
    if (retEnum.size() > 1) {
      llvm::errs() << "In " << FuncName << ": multiple return enum types.\n";
    }
    for (auto ED : retEnum) {
      for (auto EnumVal : ED->enumerators()) {
        if (isGoodEnumName(EnumVal->getName())) {
          SuccRetValMap.insert(
            std::make_pair(FuncName,EnumVal->getInitVal().getZExtValue()));
          break;
        }
      }
    }
  } else if (!retVar.empty()){
    // returns a local var
    llvm::SmallSet<const clang::EnumDecl*, 2> collectedEnum;
    for (auto RV : retVar) {
      if (varDeclWithEnum.find(RV) != varDeclWithEnum.end()) {
        collectedEnum.insert(varDeclWithEnum[RV]);
      }
      if (varStoredWithEnum.find(RV) != varStoredWithEnum.find(RV)) {
        collectedEnum.insert(varStoredWithEnum[RV]);
      }
    }
    if (!collectedEnum.empty()) {
      if (collectedEnum.size() > 1) {
        llvm::errs() << "In " << FuncName << ": multiple return enum types.\n";
      }
      for (auto ED : collectedEnum) {
        for (auto EnumVal : ED->enumerators()) {
          if (isGoodEnumName(EnumVal->getName())) {
            SuccRetValMap.insert(
              std::make_pair(FuncName,
                                EnumVal->getInitVal().getZExtValue()));
            break;
          }
        }
      }
    }
  }
  return Ret;
}

// case a) Initialize using an enum constant
bool PerryVisitor::TraverseVarDecl(VarDecl *VD) {
  // only focus on local vars
  if (!VD->isLocalVarDecl()) {
    return true;
  }
  if (VD->hasInit() && VD->getInitStyle() == VarDecl::CInit) {
    refVal = nullptr;
    auto Ret = RecursiveASTVisitor::TraverseStmt(VD->getInit());
    if (refVal) {
      EnumConstantDecl *enumVal = dyn_cast<EnumConstantDecl>(refVal);
      if (enumVal) {
        // init using an enum
        auto it = EnumValToDecl.find(enumVal);
        assert(it != EnumValToDecl.end());
        varDeclWithEnum.insert(std::make_pair(VD, (*it).second));
      }
    }
    return Ret;
  }
  return true;
}

// case b) Return an enum constant
bool PerryVisitor::TraverseReturnStmt(ReturnStmt *RS) {
  refVal = nullptr;
  auto Ret = RecursiveASTVisitor::TraverseStmt(RS->getRetValue());
  if (refVal) {
    EnumConstantDecl *enumVal = dyn_cast<EnumConstantDecl>(refVal);
    if (enumVal) {
      // returns an enum
      auto it = EnumValToDecl.find(enumVal);
      assert(it != EnumValToDecl.end());
      retEnum.insert((*it).second);
    } else {
      VarDecl *target = dyn_cast<VarDecl>(refVal);
      if (target && target->isLocalVarDecl()) {
        // returns a local var
        retVar.insert(target);
      }
    }
  }
  return Ret;
}

// case c) Assign with an enum constant
bool PerryVisitor::TraverseBinaryOperator(BinaryOperator *BO) {
  if (BO->getOpcode() == BO_Assign) {
    refVal = nullptr;
    auto Ret = RecursiveASTVisitor::TraverseStmt(BO->getRHS());
    if (!Ret) {
      return false;
    }
    const EnumDecl *enumDef = nullptr;
    if (refVal) {
      EnumConstantDecl *enumVal = dyn_cast<EnumConstantDecl>(refVal);
      if (enumVal) {
        auto it = EnumValToDecl.find(enumVal);
        assert(it != EnumValToDecl.end());
        enumDef = (*it).second;
      }
    }
    if (enumDef) {
      refVal = nullptr;
      Ret = RecursiveASTVisitor::TraverseStmt(BO->getLHS());
      if (!Ret) {
        return false;
      }
      if (refVal) {
        VarDecl *target = dyn_cast<VarDecl>(refVal);
        if (target && target->isLocalVarDecl()) {
          // stores an enum
          varStoredWithEnum.insert(std::make_pair(target, enumDef));
        }
      }
    }
    return Ret;
  }
  return true;
}

bool PerryVisitor::VisitDeclRefExpr(DeclRefExpr *DRE) {
  refVal = DRE->getDecl();
  return true;
}

// YAML I/O
struct PerryFuncRetItem {
  std::string FuncName;
  uint64_t SuccVal;
  PerryFuncRetItem(const std::string &FuncName, uint64_t SuccVal)
    : FuncName(FuncName), SuccVal(SuccVal) {}
  PerryFuncRetItem() = default;
};

struct PerryApiItem {
  std::string FuncName;
  PerryApiItem(const std::string &FuncName) : FuncName(FuncName) {}
  PerryApiItem() = default;
};

template<>
struct llvm::yaml::MappingTraits<PerryFuncRetItem> {
  static void mapping(IO &io, PerryFuncRetItem &item) {
    io.mapRequired("func", item.FuncName);
    io.mapRequired("succ_val", item.SuccVal);
  }
};

template<>
struct llvm::yaml::MappingTraits<PerryApiItem> {
  static void mapping(IO &io, PerryApiItem &item) {
    io.mapRequired("api", item.FuncName);
  }
};

template<>
struct llvm::yaml::MappingTraits<PerryLoopItem> {
  static void mapping(IO &io, PerryLoopItem &item) {
    io.mapRequired("file", item.FilePath);
    io.mapRequired("begin_line", item.beginLine);
    io.mapRequired("begin_column", item.beginColumn);
    io.mapRequired("end_line", item.endLine);
    io.mapRequired("end_column", item.endColumn);
  }
};

LLVM_YAML_IS_SEQUENCE_VECTOR(PerryFuncRetItem)
LLVM_YAML_IS_SEQUENCE_VECTOR(PerryApiItem)
LLVM_YAML_IS_SEQUENCE_VECTOR(PerryLoopItem)

// PerryASTConsumer implementation
PerryASTConsumer::PerryASTConsumer(ASTContext &Context,
                                   CompilerInstance &CI,
                                   const std::string &outFileSuccRet,
                                   const std::string &outFileApi,
                                   const std::string &outFileLoops,
                                   const std::string &outFileStructNames)
  : CI(CI), EnumMatcher(EnumValToDecl),
    LoopMatcher(CI.getSourceManager(), Loops),
    Visitor(&Context, SuccRetValMap, EnumValToDecl, FuncDec, FuncDef),
    outFileSuccRet(outFileSuccRet),
    outFileApi(outFileApi),
    outFileLoops(outFileLoops),
    outFileStructNames(outFileStructNames) {
  // Enum
  DeclarationMatcher EnumDef = enumDecl().bind("EnumDef");
  Matcher.addMatcher(EnumDef, &EnumMatcher);

  StatementMatcher ForLoop = forStmt().bind("ForLoop");
  StatementMatcher WhileLoop = whileStmt().bind("WhileLoop");
  Matcher.addMatcher(ForLoop, &LoopMatcher);
  Matcher.addMatcher(WhileLoop, &LoopMatcher);
}

void PerryASTConsumer::updateCache(CacheType ty) {
  DiagnosticsEngine &D = CI.getDiagnostics();
  std::string CacheName;
  std::function<void(PerryASTConsumer*)> loader;
  std::function<void(PerryASTConsumer*)> writer;
  switch (ty) {
    case SuccRet:
      CacheName = outFileSuccRet;
      loader = &PerryASTConsumer::SuccRetCacheLoader;
      writer = &PerryASTConsumer::SuccRetCacheWriter;
      break;
    case Api:
      CacheName = outFileApi;
      loader = &PerryASTConsumer::ApiCacheLoader;
      writer = &PerryASTConsumer::ApiCacheWriter;
      break;
    case Loop:
      CacheName = outFileLoops;
      loader = &PerryASTConsumer::LoopCacheLoader;
      writer = &PerryASTConsumer::LoopCacheWriter;
      break;
    case StructName:
      CacheName = outFileStructNames;
      loader = &PerryASTConsumer::StructCacheLoader;
      writer = &PerryASTConsumer::StructCacheWriter;
      break;
  }
  while (true) {
    llvm::LockFileManager Locked(CacheName);
    switch (Locked) {
      case llvm::LockFileManager::LFS_Error: {
        D.Report(diag::remark_module_lock_failure)
          << "Failed to acquire lock for" << CacheName;
        Locked.unsafeRemoveLockFile();
        LLVM_FALLTHROUGH;
      }
      case llvm::LockFileManager::LFS_Owned: {
        // we own the lock
        loader(this);
        writer(this);
        return;
      }
      case llvm::LockFileManager::LFS_Shared: {
        // others own the lock, wait
        switch (Locked.waitForUnlock()) {
          case llvm::LockFileManager::Res_Success: {
            // try again
            continue;
          }
          case llvm::LockFileManager::Res_OwnerDied: {
            // try again
            continue;
          }
          case llvm::LockFileManager::Res_Timeout: {
            // try again
            D.Report(diag::remark_module_lock_timeout)
              << "Timeout when wait for " << CacheName << "to unlock";
            // Locked.unsafeRemoveLockFile();
            continue;
          }
        }
        break;
      }
    }
  }
}

void PerryASTConsumer::SuccRetCacheLoader() {
  if (llvm::sys::fs::exists(outFileSuccRet)) {
    auto Result = llvm::MemoryBuffer::getFile(outFileSuccRet);
    if (bool(Result)) {
      std::vector<PerryFuncRetItem> ReadItem;
      llvm::yaml::Input yin(Result->get()->getMemBufferRef());
      yin >> ReadItem;

      if (bool(yin.error())) {
        llvm::errs() << "Failed to read data from "
                    << outFileSuccRet
                    << "\n";
      } else {
        for (auto &RI : ReadItem) {
          SuccRetValMap.insert(std::make_pair(RI.FuncName, RI.SuccVal));
        }
      }
    }
  }
}

void PerryASTConsumer::ApiCacheLoader() {
  if (llvm::sys::fs::exists(outFileApi)) {
    auto Result = llvm::MemoryBuffer::getFile(outFileApi);
    if (bool(Result)) {
      std::vector<PerryApiItem> ReadItem;
      llvm::yaml::Input yin(Result->get()->getMemBufferRef());
      yin >> ReadItem;

      if (bool(yin.error())) {
        llvm::errs() << "Failed to read data from "
                     << outFileApi
                     << "\n";
      } else {
        for (auto &RI : ReadItem) {
          FuncDec.insert(RI.FuncName);
          FuncDef.insert(RI.FuncName);
        }
      }
    }
  }
}

void PerryASTConsumer::LoopCacheLoader() {
  if (llvm::sys::fs::exists(outFileLoops)) {
    auto Result = llvm::MemoryBuffer::getFile(outFileLoops);
    if (bool(Result)) {
      std::vector<PerryLoopItem> ReadItem;
      llvm::yaml::Input yin(Result->get()->getMemBufferRef());
      yin >> ReadItem;

      if (bool(yin.error())) {
        llvm::errs() << "Failed to read data from "
                     << outFileLoops
                     << "\n";
      } else {
        for (auto &RI : ReadItem) {
          AllLoops.insert(PerryLoopItem(RI.FilePath,
                                          RI.beginLine, RI.beginColumn,
                                          RI.endLine, RI.endColumn));
        }
      }
    }
  }
}

void PerryASTConsumer::StructCacheLoader() {
  if (llvm::sys::fs::exists(outFileStructNames)) {
    auto Result = llvm::MemoryBuffer::getFile(outFileStructNames);
    if (bool(Result)) {
      std::vector<std::string> ReadItem;
      llvm::yaml::Input yin(Result->get()->getMemBufferRef());
      yin >> ReadItem;

      if (bool(yin.error())) {
        llvm::errs() << "Failed to read data from "
                     << outFileLoops
                     << "\n";
      } else {
        for (auto &RI : ReadItem) {
          periphStructNames.insert(RI);
        }
      }
    }
  }
}

void PerryASTConsumer::SuccRetCacheWriter() {
  std::vector<PerryFuncRetItem> AllItem;
  for (auto &p : SuccRetValMap) {
    AllItem.emplace_back(PerryFuncRetItem(p.first, p.second));
  }
  std::error_code ErrCode;
  llvm::raw_fd_ostream fout(outFileSuccRet, ErrCode);
  if (fout.has_error()) {
    llvm::errs() << "Failed to open "
                 << outFileSuccRet 
                 << " for write: "
                 << ErrCode.message() << "\nData lost\n";
    return;
  }
  llvm::yaml::Output yout(fout);
  yout << AllItem;
}

void PerryASTConsumer::ApiCacheWriter() {
  std::vector<std::string> HalAPI;
  std::error_code ErrCode;
  std::set_intersection(FuncDec.begin(), FuncDec.end(),
                        FuncDef.begin(), FuncDef.end(),
                        std::inserter(HalAPI, HalAPI.begin()));
  std::vector<PerryApiItem> OutAPI;
  for (auto &Name : HalAPI) {
    OutAPI.emplace_back(PerryApiItem(Name));
  }
  llvm::raw_fd_ostream fout(outFileApi, ErrCode);
  if (fout.has_error()) {
    llvm::errs() << "Failed to open "
                 << outFileApi 
                 << " for write: "
                 << ErrCode.message() << "\nData lost\n";
    return;
  }
  llvm::yaml::Output yout(fout);
  yout << OutAPI;
}

void PerryASTConsumer::LoopCacheWriter() {
  std::vector<PerryLoopItem> HalLoops;
  auto &SM = CI.getSourceManager();
  for (auto &L : Loops) {
    auto beginLoc = SourceLocation::getFromRawEncoding(L.first);
    auto endLoc = SourceLocation::getFromRawEncoding(L.second);
    if (!beginLoc.isValid() || !endLoc.isValid()) {
      continue;
    }
    if (!beginLoc.isFileID() || !endLoc.isFileID()) {
      continue;
    }
    PresumedLoc BL = SM.getPresumedLoc(beginLoc);
    PresumedLoc EL = SM.getPresumedLoc(endLoc);
    if (BL.isInvalid() || EL.isInvalid()) {
      continue;
    }
    if (BL.getFilename() != EL.getFilename()) {
      continue;
    }
    llvm::SmallString<128> abs_path = StringRef(EL.getFilename());
    std::error_code err_code = llvm::sys::fs::make_absolute(abs_path);
    if (err_code) {
      continue;
    }
    llvm::SmallString<128> real_path;
    err_code = llvm::sys::fs::real_path(abs_path, real_path, true);
    if (err_code) {
      continue;
    }
    AllLoops.insert(PerryLoopItem(real_path.str().str(),
                                     BL.getLine(), BL.getColumn(),
                                     EL.getLine(), EL.getColumn()));
  }
  for (auto &PI : AllLoops) {
    HalLoops.push_back(PI);
  }
  std::error_code ErrCode;
  llvm::raw_fd_ostream fout(outFileLoops, ErrCode);
  if (fout.has_error()) {
    llvm::errs() << "Failed to open "
                 << outFileLoops 
                 << " for write: "
                 << ErrCode.message() << "\nData lost\n";
    return;
  }
  llvm::yaml::Output yout(fout);
  yout << HalLoops;
}

void PerryASTConsumer::StructCacheWriter() {
  std::error_code ErrCode;
  std::vector<std::string> OutStructNames(periphStructNames.begin(),
                                          periphStructNames.end());
  llvm::raw_fd_ostream fout(outFileStructNames, ErrCode);
  if (fout.has_error()) {
    llvm::errs() << "Failed to open "
                 << outFileApi 
                 << " for write: "
                 << ErrCode.message() << "\nData lost\n";
    return;
  }
  llvm::yaml::Output yout(fout);
  yout << OutStructNames;
}

void PerryASTConsumer::HandleTranslationUnit(ASTContext &Context) {
  // run matcher first to collect enums
  Matcher.matchAST(Context);
  // then run visitors
  Visitor.TraverseDecl(Context.getTranslationUnitDecl());

  // dump collected data in YAML format
  updateCache(SuccRet);
  updateCache(Api);
  updateCache(Loop);
  updateCache(StructName);
}

// PerryIncludeProcessor implementation
PerryIncludeProcessor::PerryIncludeProcessor(std::set<std::string> &Inc)
  : Inc(Inc) {}

void PerryIncludeProcessor::
InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                   llvm::StringRef FileName, bool IsAngled,
                   CharSourceRange FilenameRange, const FileEntry *File,
                   llvm::StringRef SearchPath, llvm::StringRef RelativePath,
                   const Module *Imported,
                   SrcMgr::CharacteristicKind FileType) {
  // discard std libs
  if (IsAngled) {
    return;
  }

  Inc.insert(FileName.str());
  
}

// PerryPeriphStructDefProcessor implementation
PerryPeriphStructDefProcessor::
PerryPeriphStructDefProcessor(std::set<std::string> &periphStructNames)
  : periphStructNames(periphStructNames) {}

void PerryPeriphStructDefProcessor::MacroExpands(const Token &MacroNameTok,
                                                 const MacroDefinition &MD,
                                                 SourceRange Range,
                                                 const MacroArgs *Args) {
  if (Args && Args->getNumMacroArguments()) {
    return;
  }

  auto MI = MD.getMacroInfo();
  if (MI->getNumParams()) {
    return;
  }

  if (MI->tokens_empty()) {
    return;
  }

  if (!MacroNameTok.is(tok::identifier)) {
    return;
  }

  enum parsing_stage {
    begin = 0,
    first_lp_seen,
    second_lp_seen,
    struct_seen,
    star_seen,
    first_rp_seen,
    numeric_seen,
    ident_seen,
    second_rp_seen
  };

  parsing_stage state = begin;
  std::string struct_name;
  for (auto &token : MI->tokens()) {
    switch (state) {
      case begin: {
        if (token.is(tok::l_paren)) {
          state = first_lp_seen;
          break;
        } else {
          return;
        }
      }
      case first_lp_seen: {
        if (token.is(tok::l_paren)) {
          state = second_lp_seen;
          break;
        } else {
          return;
        }
      }
      case second_lp_seen: {
        if (token.is(tok::identifier)) {
          struct_name = token.getIdentifierInfo()->getName().str();
          state = struct_seen;
          break;
        } else {
          return;
        }
      }
      case struct_seen: {
        if (token.is(tok::star)) {
          state = star_seen;
          break;
        } else {
          return;
        }
      }
      case star_seen: {
        if (token.is(tok::r_paren)) {
          state = first_rp_seen;
          break;
        } else {
          return;
        }
      }
      case first_rp_seen: {
        if (token.is(tok::numeric_constant)) {
          state = numeric_seen;
          break;
        } else if (token.is(tok::identifier)) {
          state = ident_seen;
          break;
        } else {
          return;
        }
      }
      case numeric_seen:
      case ident_seen: {
        if (token.is(tok::r_paren)) {
          state = second_rp_seen;
          break;
        } else {
          return;
        }
      }
      case second_rp_seen: break;
    }

    if (state == second_rp_seen) {
      break;
    }
  }

  if (state != second_rp_seen) {
    return;
  }

  periphStructNames.insert(struct_name);
}


// FrontendAction
class PerryPluginAction : public PluginASTAction {
public:
  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &arg) override {
    DiagnosticsEngine &D = CI.getDiagnostics();
    auto num_args = arg.size();
    for (size_t i = 0; i < num_args; ++i) {
      if (arg[i] == "-out-file-succ-ret") {
        if (i + 1 >= num_args) {
          D.Report(
            D.getCustomDiagID(DiagnosticsEngine::Error,
                              "missing -out-file-succ-ret argument"));
          return false;
        }
        ++i;
        outFileSuccRet = arg[i];
      } else if (arg[i] == "-out-file-api") {
        if (i + 1 >= num_args) {
          D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                     "missing -out-file-api argument"));
          return false;
        }
        ++i;
        outFileApi = arg[i];
      } else if (arg[i] == "-out-file-loops") {
        if (i + 1 >= num_args) {
          D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                     "missing -out-file-loops argument"));
          return false;
        }
        ++i;
        outFileLoops = arg[i];
      } else if (arg[i] == "-out-file-periph-struct") {
        if (i + 1 >= num_args) {
          D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                     "missing -out-file-periph-struct argument"));
          return false;
        }
        ++i;
        outFileStructNames = arg[i];
      }
    }

    if (outFileSuccRet.empty()) {
      D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                 "missing -out-file-succ-ret argument"));
      return false;
    }
    if (outFileApi.empty()) {
      D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                 "missing -out-file-api argument"));
      return false;
    }
    if (outFileLoops.empty()) {
      D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                 "missing -out-file-loops argument"));
      return false;
    }
    if (outFileStructNames.empty()) {
      D.Report(D.getCustomDiagID(DiagnosticsEngine::Error,
                                 "missing -out-file-periph-struct argument"));
      return false;
    }
    return true;
  }

  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InFile) override {
    // CI.getPreprocessor().addPPCallbacks(
    //   std::make_unique<PerryIncludeProcessor>(Inc));
    auto ret = std::make_unique<PerryASTConsumer>(
        CI.getASTContext(), CI, outFileSuccRet, outFileApi,
        outFileLoops, outFileStructNames);
    CI.getPreprocessor().addPPCallbacks(
      std::make_unique<PerryPeriphStructDefProcessor>(ret->getStructNames()));
    return ret;
      
  }

  ActionType getActionType() override {
    return AddBeforeMainAction;
  }

private:
  std::string outFileSuccRet;
  std::string outFileApi;
  std::string outFileLoops;
  std::string outFileStructNames;
};

// register FrontendAction
static FrontendPluginRegistry::Add<PerryPluginAction>
  X("perry", "Perry clang plugin");