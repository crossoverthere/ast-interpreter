//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame
{
	/// StackFrame maps Variable Declaration to Value 将变量声明映射为数值
	/// Which are either integer or addresses (also represented using an Integer value) 变量或者地址(也可能常量)
	std::map<Decl *, int> mVars;
	std::map<Stmt *, int> mExprs;
	/// The current stmt 当前stmt
	Stmt *mPC;

public:
	StackFrame() : mVars(), mExprs(), mPC()
	{
	}

	void bindDecl(Decl *decl, int val)
	{
		// 变量赋值
		mVars[decl] = val;
	}
	int getDeclVal(Decl *decl)
	{
		assert(mVars.find(decl) != mVars.end());
		return mVars.find(decl)->second;
	}
	void bindStmt(Stmt *stmt, int val)
	{
		// 操作结果
		mExprs[stmt] = val;
	}
	int getStmtVal(Stmt *stmt)
	{
		// 获取栈帧中操作数
		assert(mExprs.find(stmt) != mExprs.end());
		return mExprs[stmt];
	}
	void setPC(Stmt *stmt)
	{
		mPC = stmt;
	}
	Stmt *getPC()
	{
		return mPC;
	}
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment
{
	std::vector<StackFrame> mStack; // 栈帧，用于保存节点序列？

	FunctionDecl *mFree; /// Declartions to the built-in functions
	FunctionDecl *mMalloc;
	FunctionDecl *mInput;
	FunctionDecl *mOutput;

	FunctionDecl *mEntry;

public:
	/// Get the declartions to the built-in functions
	Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL)
	{
	}

	/// Initialize the Environment
	void init(TranslationUnitDecl *unit)
	{
		for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i)
		{
			if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i))
			{
				if (fdecl->getName().equals("FREE"))
					mFree = fdecl;
				else if (fdecl->getName().equals("MALLOC"))
					mMalloc = fdecl;
				else if (fdecl->getName().equals("GET"))
					mInput = fdecl;
				else if (fdecl->getName().equals("PRINT"))
					mOutput = fdecl;
				else if (fdecl->getName().equals("main"))
					mEntry = fdecl;
			}
		}
		mStack.push_back(StackFrame());
	}

	FunctionDecl *getEntry()
	{
		return mEntry;
	}

	/*添加代码，用于实现节点访问细节操作*/
	int getValue(Expr *expr)
	{
		// 此函数用于为外部返回栈帧内部相关变量
		return mStack.back().getStmtVal(expr);
	}
	void literal(IntegerLiteral *literal)
	{
		// 将常量存入栈帧
		int val = literal->getValue().getSExtValue();
		mStack.back().bindStmt(literal, val);
	}
	/**/

	/// !TODO Support comparison operation
	void binop(BinaryOperator *bop)
	{
		Expr *left = bop->getLHS();
		Expr *right = bop->getRHS();

		if (bop->isAssignmentOp())
		{
			int val = mStack.back().getStmtVal(right);
			mStack.back().bindStmt(left, val);
			if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left))
			{
				// 为变量赋予新值
				Decl *decl = declexpr->getFoundDecl();
				mStack.back().bindDecl(decl, val);
			}
		}
		else
		{	
			// 其余二元操作实现
			clang::BinaryOperator::Opcode op = bop->getOpcode();
			int left_value = mStack.back().getStmtVal(left);
			int right_value = mStack.back().getStmtVal(right);
			int result;
			switch (op) {
				default:

				case BO_EQ: 
					result = left_value == right_value;
					break;
			}
			// 将二元操作结果存入栈帧
			mStack.back().bindStmt(bop, result);
		}
	}

	void decl(DeclStmt *declstmt)
	{
		// 声明操作
		for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			 it != ie; ++it)
		{
			Decl *decl = *it;
			if (VarDecl *vardecl = dyn_cast<VarDecl>(decl))
			{
				if (vardecl->hasInit())
				{
					// 有初始化
					int val = mStack.back().getStmtVal(vardecl->getInit());
					mStack.back().bindDecl(vardecl, val);
				}
				else
				{
					// 无初始化，则置0
					mStack.back().bindDecl(vardecl, 0);
				}
			}
		}
	}

	void declref(DeclRefExpr *declref)
	{
		mStack.back().setPC(declref);
		if (declref->getType()->isIntegerType())
		{
			Decl *decl = declref->getFoundDecl();

			int val = mStack.back().getDeclVal(decl);
			// 将变量数值存入栈帧
			mStack.back().bindStmt(declref, val);
		}
	}

	void cast(CastExpr *castexpr)
	{
		mStack.back().setPC(castexpr);
		if (castexpr->getType()->isIntegerType())
		{
			Expr *expr = castexpr->getSubExpr();
			int val = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, val);
		}
	}

	/// !TODO Support Function Call
	void call(CallExpr *callexpr)
	{
		mStack.back().setPC(callexpr);
		int val = 0;
		FunctionDecl *callee = callexpr->getDirectCallee();
		if (callee == mInput)
		{
			llvm::errs() << "Please Input an Integer Value : ";
			scanf("%d", &val);

			mStack.back().bindStmt(callexpr, val);
		}
		else if (callee == mOutput)
		{
			Expr *decl = callexpr->getArg(0);
			val = mStack.back().getStmtVal(decl);
			llvm::errs() << val;
		}
		else
		{
			/// You could add your code here for Function call Return
		}
	}
};
