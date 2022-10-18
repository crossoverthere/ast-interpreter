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
	// mVars用于映射变量到数值
	// mExprs用于映射操作(stmt节点)到数值
	std::map<Decl *, int> mVars;
	std::map<Stmt *, int> mExprs;
	/// The current stmt 当前stmt
	Stmt *mPC;
	// 定义一个栈，用于保存函数返回值
	std::stack<int> mResult;
	// 定义一个映射，用于保存全局变量
	std::map<Decl *, int> gVars;

public:
	StackFrame() : mVars(), mExprs(), mPC()
	{
	}

	void bindDecl(Decl *decl, int val)
	{
		// 保存变量
		mVars[decl] = val;
	}
	int getDeclVal(Decl *decl)
	{
//		assert(mVars.find(decl) != mVars.end());
		return mVars.find(decl)->second;
	}
	bool hasDeclVal(Decl *decl)
	{
		return mVars.find(decl) != mVars.end();
	}
	void bindStmt(Stmt *stmt, int val)
	{
		// 保存节点值
		mExprs[stmt] = val;
	}
	int getStmtVal(Stmt *stmt)
	{
		// 获取栈帧中操作数
		assert(mExprs.find(stmt) != mExprs.end());
		return mExprs[stmt];
	}
	void bindGDecl(Decl *decl, int val)
	{
		// 保存全局变量
		gVars[decl] = val;
	}
	int getGDeclVal(Decl *decl)
	{
		return gVars.find(decl)->second;
	}
	void setGDecl(std::map<Decl *, int> *maps)
	{
		// 用于复制全局变量映射
		gVars = *maps;
	}
	std::map<Decl *, int> *getGDecl()
	{
		// 用于复制全局变量映射
		return &gVars;
	}
	void pushResult(int retVal)
	{
		// 函数返回值入栈
		mResult.push(retVal);
	}
	int popResult()
	{
		// 函数返回值出栈
		int val = mResult.top();
		mResult.pop();
		return val;
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
	std::vector<StackFrame> mStack; // 用于保存不同函数的栈帧，mStack.back()指向当前函数栈帧

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
		mStack.push_back(StackFrame());
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
			else if(VarDecl *vardecl = dyn_cast<VarDecl>(*i))
			{
				// 遍历全局变量声明
				// 将全局变量存入gVar
				if(vardecl->hasInit())
				{
					Expr *expr = vardecl->getInit();
					IntegerLiteral *literal = dyn_cast<IntegerLiteral>(expr);
					int var = literal->getValue().getSExtValue();

					mStack.back().bindGDecl(vardecl, var);
				}
				else
				{	
					mStack.back().bindGDecl(vardecl, 0);
				}
			}
		}
//		mStack.push_back(StackFrame());
	}

	FunctionDecl *getEntry()
	{
		return mEntry;
	}

	/*添加代码，用于实现节点访问细节操作*/
	int getValue(Expr *expr)
	{
		// 此函数用于为外部返回栈帧内节点映射值
		return mStack.back().getStmtVal(expr);
	}
	void literal(IntegerLiteral *literal)
	{
		// 将常量存入栈帧
		int val = literal->getValue().getSExtValue();
		mStack.back().bindStmt(literal, val);
	}
	void unaop(UnaryOperator *uop)
	{
		// 实现一元操作
		Expr *expr = uop->getSubExpr();

		if(uop->isArithmeticOp()){
			// 算术运算
			clang::UnaryOperator::Opcode op = uop->getOpcode();
			int val = mStack.back().getStmtVal(expr);
			int result;

			switch (op)
			{
			case UO_Plus:
				result = +val;
				break;
			case UO_Minus:
				result = -val;
				break;

			default:
				break;
			}
			// 运算结果存入栈帧
			mStack.back().bindStmt(uop, result);
		}
	}
	void setRetValue(ReturnStmt *retstmt)
	{
		// 将当前函数栈帧弹出
		// 将子函数返回值入栈
		Expr *retVal = retstmt->getRetValue();
		int val = mStack.back().getStmtVal(retVal);

		mStack.pop_back();
		mStack.back().pushResult(val);
	}
	void getRetValue(CallExpr *call)
	{
		// 取出子函数返回值
		// 将返回值与函数调用节点绑定
		int val = mStack.back().popResult();

		mStack.back().bindStmt(call, val);
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
				case BO_Add:
					result = left_value + right_value;
					break;
				case BO_Sub:
					result = left_value - right_value;
					break;
				case BO_GT:
					result = left_value > right_value;
					break;
				case BO_LT:
					result = left_value < right_value;
					break;
				case BO_EQ: 
					result = left_value == right_value;
					break;
				case BO_NE:
					result = left_value != right_value;
					break;
				case BO_GE:
					result = left_value >= right_value;
					break;
				case BO_LE:
					result = left_value <= right_value;
					break;
				
				default:
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
			int val;

			if(mStack.back().hasDeclVal(decl))
			{
				// 首先从mVars中查找变量
				val = mStack.back().getDeclVal(decl);
			}
			else
			{
				// 不在则查找全局变量
				val = mStack.back().getGDeclVal(decl);
			}
			// 将变量数值存入栈帧
			mStack.back().bindStmt(declref, val);
		}
	}

	void cast(CastExpr *castexpr)
	{
		mStack.back().setPC(castexpr);
		if (castexpr->getType()->isIntegerType())
		{
			// ImplicitCastExpr类型为整形
			// 从栈帧中读出子结点映射值，绑定该节点
			// 是因为父节点获取变量值时，只能从子结点的栈帧获得
			Expr *expr = castexpr->getSubExpr();
			int val = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, val);
			
		}
	}

	/// !TODO Support Function Call
	// 利用返回值判定所调用函数的类型
	int call(CallExpr *callexpr)
	{
		mStack.back().setPC(callexpr);
		int val = 0;
		FunctionDecl *callee = callexpr->getDirectCallee();
		if (callee == mInput)
		{
			llvm::errs() << "Please Input an Integer Value : ";
			scanf("%d", &val);

			mStack.back().bindStmt(callexpr, val);
			return 0;
		}
		else if (callee == mOutput)
		{
			// 获取函数传递参数
			Expr *decl = callexpr->getArg(0);
			val = mStack.back().getStmtVal(decl);
			llvm::errs() << val;
			return 0;
		}
		else
		{
			/// You could add your code here for Function call Return
			// 此处必须开一个新栈帧，用于区分多次调用时的同名变量
			// 传递函数参数
			StackFrame funFrame = StackFrame();

			int paramNum = callee->getNumParams();
			assert(paramNum == callexpr->getNumArgs());
			for(int i=0; i < paramNum; i++)
			{
				// 将子函数参数与传入数值相绑定
				Expr *arg = callexpr->getArg(i);
				val = mStack.back().getStmtVal(arg);
				ParmVarDecl *parm = callee->getParamDecl(i);

				funFrame.bindDecl(parm, val);
			}
			// 复制全局变量到新的栈帧
			funFrame.setGDecl(mStack.back().getGDecl());
			// 将函数栈帧压入
			mStack.push_back(funFrame);
			// 调用子函数，返回1
			return 1;
		}
	}
};
