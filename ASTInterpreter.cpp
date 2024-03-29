//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class ReturnException : public std::exception {};

// 提取AST节点信息
// 需要编写这一部分实现RecursiveASTVisitor
// VisitStmt访问所有子结点，Visit访问当前节点
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
      // 处理ImplicitCastExpr节点
      VisitStmt(expr);
      mEnv->cast(expr);
   }
   virtual void VisitCallExpr(CallExpr *call)
   {
      // 函数调用
      VisitStmt(call);
      if(mEnv->call(call))
      {
         // 转入子函数函数体节点
         try{
            Visit(call->getDirectCallee()->getBody());
         }
         catch(ReturnException e){}
         mEnv->getRetValue(call);
      }
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
   // CompoundStmt节点
   virtual void VisitCompoundStmt(CompoundStmt *copstmt)
   {
      // 函数体节点，无需操作
      VisitStmt(copstmt);
   }
   // IfStmt节点
   virtual void VisitIfStmt(IfStmt *ifstmt)
   {
      // 不可直接取出ifstmt所有子结点
      // 先取出判断运算节点
      Expr *cond = ifstmt->getCond();
      Visit(cond);
      // 创建一个函数取出栈帧内保存的二元操作结果
      if(mEnv->getValue(cond))
      {
         // 此处不可用VisitStmt()，因为存在函数体缺少CompoundStmt节点的情况
         Visit(ifstmt->getThen());
      }
      else
      {
         if(ifstmt->getElse())
         {
            Visit(ifstmt->getElse());
         }
      }
   }
   // WhileStmt节点
   virtual void VisitWhileStmt(WhileStmt *wilstmt)
   {
      // 先取出判断运算节点
      Expr *cond = wilstmt->getCond();
      Visit(cond);
      // 直到判断运算返回假，结束循环
      while(mEnv->getValue(cond))
      {
         Visit(wilstmt->getBody());
         Visit(cond);
      }
   }
   // ForStmt节点
   virtual void VisitForStmt(ForStmt *forstmt)
   {
      // 同while，当判断运算返回假时，结束循环
      Expr *cond = forstmt->getCond();
      

      if(forstmt->getInit())
      {
         Visit(forstmt->getInit());
      }
      Visit(cond);
      while(mEnv->getValue(cond))
      {
         Visit(forstmt->getBody());
         Visit(forstmt->getInc());
         Visit(cond);
      }
   }
   // ArraySubscriptExpr节点
   virtual void VisitArraySubscriptExpr(ArraySubscriptExpr *array)
   {
      VisitStmt(array);
      mEnv->arrayexpr(array);
   }
   // CStyleCastExpr节点
   virtual void VisitCStyleCastExpr(CStyleCastExpr *cscast)
   {
      // 用于转换类型
      // 此处不做处理，仅传递数据
      VisitStmt(cscast);
      mEnv->cast(cscast);
   }
   // UnaryExprOrTypeTraitExpr节点
   virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *typetrait)
   {
      VisitStmt(typetrait);
      mEnv->uett(typetrait);
   }
   // ParenExpr节点
   virtual void VisitParenExpr(ParenExpr *parenexpr)
   {
      VisitStmt(parenexpr);
      mEnv->paren(parenexpr);
   }
   // ReturnStmt节点
   virtual void VisitReturnStmt(ReturnStmt *retstmt)
   {
      VisitStmt(retstmt);

      // 保存返回值
      mEnv->setRetValue(retstmt);
      // 函数返回后，程序会继续执行子函数后续节点
      // 这里利用抛出异常，终止代码块的执行
      throw ReturnException();
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

      try{
         mVisitor.VisitStmt(entry->getBody());
      }
      catch(ReturnException e){}
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
