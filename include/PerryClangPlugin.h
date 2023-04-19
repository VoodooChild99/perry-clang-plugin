#pragma once

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/PPCallbacks.h"

using EnumMapTy 
  = std::map<const clang::EnumConstantDecl*, const clang::EnumDecl*>;

using LoopRangeSet
   = std::set<std::pair<clang::SourceLocation::UIntTy,
                        clang::SourceLocation::UIntTy>>;

// ASTMatcher callback when enum is matched
class PerryEnumMatcher 
  : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  explicit PerryEnumMatcher(EnumMapTy &);
  void run(const clang::ast_matchers::MatchFinder::MatchResult &) override;

private:
  EnumMapTy &EnumValToDecl;
};

// ASTMatcher callback when for loop is matched
class PerryLoopMatcher 
  : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  explicit PerryLoopMatcher(clang::SourceManager &, LoopRangeSet &);
  void run(const clang::ast_matchers::MatchFinder::MatchResult &) override;
private:
  clang::SourceManager &SM;
  LoopRangeSet &Loops;
};

// RecursiveASTVisitor
class PerryVisitor : public clang::RecursiveASTVisitor<PerryVisitor> {
public:
  explicit PerryVisitor(clang::ASTContext *Context,
                        std::map<std::string, uint64_t> &SuccRetValMap,
                        const EnumMapTy &EnumValToDecl,
                        std::set<std::string> &FuncDec,
                        std::set<std::string> &FuncDef)
    : Context(Context),
      SuccRetValMap(SuccRetValMap),
      EnumValToDecl(EnumValToDecl),
      FuncDec(FuncDec),
      FuncDef(FuncDef) {}
  // traverse all function
  bool TraverseFunctionDecl(clang::FunctionDecl *FD);
  // traverse return statements
  bool TraverseReturnStmt(clang::ReturnStmt *RS);
  // traverse variable declaration statements
  bool TraverseVarDecl(clang::VarDecl *VD);
  // traverse binary operators
  bool TraverseBinaryOperator(clang::BinaryOperator *BO);
  // visit return statements
  bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
private:
  clang::ASTContext *Context;
  std::map<std::string, uint64_t> &SuccRetValMap;
  const EnumMapTy &EnumValToDecl;
  std::set<std::string> &FuncDec;
  std::set<std::string> &FuncDef;

  clang::ValueDecl *refVal = nullptr;
  llvm::SmallSet<const clang::EnumDecl*, 2> retEnum;
  llvm::SmallSet<const clang::VarDecl*, 2> retVar;
  llvm::SmallMapVector<const clang::VarDecl*, const clang::EnumDecl*, 16> varDeclWithEnum;
  llvm::SmallMapVector<const clang::VarDecl*, const clang::EnumDecl*, 16> varStoredWithEnum;
  bool isGoodEnumName(const llvm::StringRef &);
};

struct PerryLoopItem {
  std::string FilePath;
  unsigned beginLine = 0;
  unsigned beginColumn = 0;
  unsigned endLine = 0;
  unsigned endColumn = 0;

  PerryLoopItem(const std::string &FilePath, unsigned beginLine,
                unsigned beginColumn, unsigned endLine, unsigned endColumn)
    : FilePath(FilePath), beginLine(beginLine), beginColumn(beginColumn),
      endLine(endLine), endColumn(endColumn) {}
  PerryLoopItem() = default;
  PerryLoopItem(const PerryLoopItem &PI)
    : FilePath(PI.FilePath),
      beginLine(PI.beginLine), beginColumn(PI.beginColumn),
      endLine(PI.endLine), endColumn(PI.endColumn) {}

  int compare(const PerryLoopItem &PI) const {
    int file_cmp = FilePath.compare(PI.FilePath);
    if (!file_cmp) {
      uint64_t begin = (((uint64_t)beginLine << 32) | beginColumn);
      uint64_t begin_pi = (((uint64_t)PI.beginLine << 32) | PI.beginColumn);
      if (begin == begin_pi) {
        uint64_t end = (((uint64_t)endLine << 32) | endColumn);
        uint64_t end_pi = (((uint64_t)PI.endLine << 32) | PI.endColumn);
        if (end == end_pi) {
          return 0;
        } else if (end < end_pi) {
          return -1;
        } else {
          return 1;
        }
      } else if (begin < begin_pi) {
        return -1;
      } else {
        return 1;
      }
    } else {
      return file_cmp;
    }
  }

  bool operator==(const PerryLoopItem &PI) const {
    return (this->compare(PI) == 0);
  }

  bool operator<(const PerryLoopItem &PI) const {
    return (this->compare(PI) < 0);
  }
};

// ASTConsumer
class PerryASTConsumer : public clang::ASTConsumer {
public:
  PerryASTConsumer(clang::ASTContext &Context,
                   clang::CompilerInstance &CI,
                   const std::string &outFileSuccRet,
                   const std::string &outFileApi,
                   const std::string &outFileLoops,
                   const std::string &outFileStructNames);
  void HandleTranslationUnit(clang::ASTContext &Context) override;

private:
  clang::CompilerInstance &CI;
  clang::ast_matchers::MatchFinder Matcher;
  EnumMapTy EnumValToDecl;
  std::map<std::string, uint64_t> SuccRetValMap;
  LoopRangeSet Loops;
  PerryEnumMatcher EnumMatcher;
  PerryLoopMatcher LoopMatcher;
  PerryVisitor Visitor;
  std::string outFileSuccRet;
  std::string outFileApi;
  std::string outFileLoops;
  std::string outFileStructNames;
  std::set<std::string> FuncDec;
  std::set<std::string> FuncDef;
  std::set<PerryLoopItem> AllLoops;
  std::set<std::string> periphStructNames;

  enum CacheType {
    SuccRet = 0,
    Api,
    Loop,
    StructName
  };

  void updateCache(CacheType ty);

  void SuccRetCacheLoader();
  void ApiCacheLoader();
  void LoopCacheLoader();
  void StructCacheLoader();

  void SuccRetCacheWriter();
  void ApiCacheWriter();
  void LoopCacheWriter();
  void StructCacheWriter();

public:
  std::set<std::string> &getStructNames() { return periphStructNames; }
};

// PerryIncludeProcessor
class PerryIncludeProcessor : public clang::PPCallbacks {
public:
  PerryIncludeProcessor(std::set<std::string> &);
  void InclusionDirective(clang::SourceLocation HashLoc,
                          const clang::Token &IncludeTok,
                          llvm::StringRef FileName,
                          bool IsAngled,
                          clang::CharSourceRange FilenameRange,
                          const clang::FileEntry *File,
                          llvm::StringRef SearchPath,
                          llvm::StringRef RelativePath,
                          const clang::Module *Imported,
                          clang::SrcMgr::CharacteristicKind FileType) override;

private:
  // clang::CompilerInstance &CI;
  std::set<std::string> &Inc;
};

// PerryPeriphStructDefProcessor
class PerryPeriphStructDefProcessor : public clang::PPCallbacks {
public:
  PerryPeriphStructDefProcessor(std::set<std::string> &);
  void MacroExpands(const clang::Token &MacroNameTok,
                    const clang::MacroDefinition &MD,
                    clang::SourceRange Range,
                    const clang::MacroArgs *Args) override;

private:
  std::set<std::string> &periphStructNames;
};