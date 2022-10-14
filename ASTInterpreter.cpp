//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

// 提取AST节点信息
// 需要编写这一部分实现RecursiveASTVisitor
// 仅遍历stmt节点，无Decl节点
class InterpreterVisitor : public EvaluatedExprVisitor<InterpreterVisitor>
{
public:
   explicit InterpreterVisitor(const ASTContext &context, Environment *env)
       : EvaluatedExprVisitor(context), mEnv(env) {}
   virtual ~InterpreterVisitor() {}

   virtual void VisitBinaryOperator(BinaryOperator *bop)
   {
      VisitStmt(bop);
      mEnv->binop(bop);
   }
   virtual void VisitDeclRefExpr(DeclRefExpr *expr)
   {
      VisitStmt(expr);
      mEnv->declref(expr);
   }
   virtual void VisitCastExpr(CastExpr *expr)
   {
      VisitStmt(expr);
      mEnv->cast(expr);
   }
   virtual void VisitCallExpr(CallExpr *call)
   {
      VisitStmt(call);
      mEnv->call(call);
   }
   virtual void VisitDeclStmt(DeclStmt *declstmt)
   {
      VisitStmt(declstmt);
      mEnv->decl(declstmt);
   }
   /*补充代码，用于访问其他类型节点*/
   // IntegerLiteral节点
   virtual void VisitIntegerLiteral(IntegerLiteral *literal)
   {
      VisitStmt(literal);
      mEnv->literal(literal);
   }
   // UnaryOperator节点
   virtual void VisitUnaryOperator(UnaryOperator *uop)
   {
      VisitStmt(uop);
      mEnv->unaop(uop);
   }
   // IfStmt节点
   virtual void VisitIfStmt(IfStmt *ifstmt)
   {
      // 不可直接取出ifstmt所有子结点
      // 先取出判断节点
      Expr *cond = ifstmt->getCond();
      Visit(cond);
      // 创建一个函数取出栈帧内保存的二元操作结果
      if(mEnv->getValue(cond))
      {
         VisitStmt(ifstmt->getThen());
      }
      else
      {
         VisitStmt(ifstmt->getElse());
      }
   }
   /**/

private:
   Environment *mEnv;
};

// 提供入口点，此处仅需要HandleTranslationUnit
class InterpreterConsumer : public ASTConsumer
{
public:
   explicit InterpreterConsumer(const ASTContext &context) : mEnv(),
                                                             mVisitor(context, &mEnv)
   {
   }
   virtual ~InterpreterConsumer() {}

   virtual void HandleTranslationUnit(clang::ASTContext &Context)
   {
      TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
      mEnv.init(decl);

      FunctionDecl *entry = mEnv.getEntry();
      mVisitor.VisitStmt(entry->getBody());
   }

private:
   Environment mEnv;
   InterpreterVisitor mVisitor;
};

// 入口，执行操作
class InterpreterClassAction : public ASTFrontendAction
{
public:
   virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
       clang::CompilerInstance &Compiler, llvm::StringRef InFile)
   {
      return std::unique_ptr<clang::ASTConsumer>(
          new InterpreterConsumer(Compiler.getASTContext()));
   }
};

int main(int argc, char **argv)
{
   if (argc > 1)
   {
      clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
   }
}
