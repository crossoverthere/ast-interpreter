//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <iostream>

using namespace clang;

class StackFrame
{
	/// StackFrame maps Variable Declaration to Value 将变量声明映射为数值
	/// Which are either integer or addresses (also represented using an Integer value) 变量或者地址(也可能常量)
	// mVars用于映射变量到数值
	// mExprs用于映射操作(节点)到数值
	// mPtrs用于映射指针到地址
	// mExprs_P用于映射节点到地址
	// 数据类型改为int64_t，便于将地址以数据形式保存
	std::map<Decl *, int64_t> mVars;
	std::map<Stmt *, int64_t> mExprs;
//	std::map<Decl *, int64_t *> mPtrs;
//	std::map<Stmt *, int *> mExprs_P;
	/// The current stmt 当前stmt
	Stmt *mPC;
	// 用于保存函数返回值
	int64_t mResult;

public:
	StackFrame() : mVars(), mExprs(), mPC()
	{
	}

	void bindDecl(Decl *decl, int64_t val)
	{
		// 保存变量
		mVars[decl] = val;
	}
	int64_t getDeclVal(Decl *decl)
	{
		assert(mVars.find(decl) != mVars.end());
		return mVars[decl];
	}
	bool hasDeclVal(Decl *decl)
	{
		return mVars.find(decl) != mVars.end();
	}
	// void bindPtr(Decl *decl, int64_t *ptr)
	// {
	// 	// 保存指针
	// 	mPtrs[decl] = ptr;
	// }
	// int64_t *getPtr(Decl *decl)
	// {
	// 	assert(mPtrs.find(decl) != mPtrs.end());
	// 	return mPtrs[decl];
	// }
	void bindStmt(Stmt *stmt, int64_t val)
	{
		// 保存节点值
		mExprs[stmt] = val;
	}
	int64_t getStmtVal(Stmt *stmt)
	{
		// 获取节点值
		assert(mExprs.find(stmt) != mExprs.end());
		return mExprs[stmt];
	}
	// void bindStmtPtr(Stmt *stmt, int *ptr)
	// {
	// 	mExprs_P[stmt] = ptr;
	// }
	// int *getStmtPtr(Stmt *stmt)
	// {
	// 	assert(mExprs_P.find(stmt) != mExprs_P.end());
	// 	return mExprs_P[stmt];
	// }
	void setResult(int64_t retVal)
	{
		// 保存函数返回值
		mResult = retVal;
	}
	int64_t getResult()
	{
		// 取出函数返回值
		int64_t val = mResult;
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
	std::map<Decl *, int64_t> gVars;  	// 用于保存全局变量

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
			else if(VarDecl *vardecl = dyn_cast<VarDecl>(*i))
			{
				// 遍历全局变量声明
				// 将全局变量存入gVar
				if(vardecl->hasInit())
				{
					Expr *expr = vardecl->getInit();
					IntegerLiteral *literal = dyn_cast<IntegerLiteral>(expr);
					int64_t var = literal->getValue().getSExtValue();

					gVars[vardecl] = var;
				}
				else
				{	
					gVars[vardecl] = 0;
				}
			}
		}
		mStack.push_back(StackFrame());
	}

	FunctionDecl *getEntry()
	{
		return mEntry;
	}

	/*添加代码，用于实现节点访问细节操作*/
	/******************************/
	int64_t getValue(Expr *expr)
	{
		// 此函数用于为外部返回栈帧内节点映射值
		return mStack.back().getStmtVal(expr);
	}
	void literal(IntegerLiteral *literal)
	{
		// 将常量存入栈帧
		int64_t val = literal->getValue().getSExtValue();
		mStack.back().bindStmt(literal, val);
	}
	void unaop(UnaryOperator *uop)
	{
		// 实现一元操作
		Expr *expr = uop->getSubExpr();
		int64_t result;

		if(uop->isArithmeticOp()){
			// 算术运算
			clang::UnaryOperator::Opcode op = uop->getOpcode();
			int64_t val = mStack.back().getStmtVal(expr);

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
		}
		else
		{
			// 非算术运算，目前仅考虑操作符*
			// 记录地址处保存的数值
			int64_t *ptr = (int64_t *)mStack.back().getStmtVal(expr);
			result = *ptr;
		}
		// 运算结果存入栈帧
		mStack.back().bindStmt(uop, result);
	}
	void arrayexpr(ArraySubscriptExpr *array)
	{
		// 这两个函数均跳过了ImplicitCastExpr
		// 直接指向DeclRefExpr
		Expr *base = array->getBase();
		Expr *idx = array->getIdx();

		int64_t *ptr = (int64_t *)mStack.back().getStmtVal(base);
		int64_t index = mStack.back().getStmtVal(idx);
		int64_t val = ptr[index];
		
		mStack.back().bindStmt(array, val);
	}
	void uett(UnaryExprOrTypeTraitExpr *typetrait)
	{
		// 目前仅考虑类型为UETT_SizeOf
		int64_t result = sizeof(int64_t);
		
		mStack.back().bindStmt(typetrait, result);
	}
	void paren(ParenExpr *paren)
	{
		// 括号节点，因此仅传递数值
		Expr *expr = paren->getSubExpr();
		int64_t val = mStack.back().getStmtVal(expr);

		mStack.back().bindStmt(paren, val);
	}
	void setRetValue(ReturnStmt *retstmt)
	{
		// 保存函数返回值
		Expr *retVal = retstmt->getRetValue();
		int64_t val = mStack.back().getStmtVal(retVal);

		mStack.back().setResult(val);
		mStack.back().bindStmt(retstmt, val);
	}
	void getRetValue(CallExpr *call)
	{
		// 取出子函数返回值
		// 弹出子函数栈帧
		// 将返回值与函数调用节点绑定
		int64_t val = mStack.back().getResult();
		mStack.pop_back();
		mStack.back().bindStmt(call, val);
	}
	/******************************/
	/******************************/

	/// !TODO Support comparison operation
	void binop(BinaryOperator *bop)
	{
		Expr *left = bop->getLHS();
		Expr *right = bop->getRHS();

		if (bop->isAssignmentOp())
		{
			// 赋值操作			
			// 左侧可能为DeclRefExpr，ArraySubscriptExpr，UnaryOperator
			// 右侧可能为ImplicitCastExpr，CStyleCastExpr，IntegerLiteral
			int64_t val = mStack.back().getStmtVal(right);

			if (isa<DeclRefExpr>(left))
			{
				DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left);
				Decl *vardecl = declexpr->getFoundDecl();

				QualType declType = declexpr->getType();
				if(declType->isIntegerType())
				{
					// 左侧为整形变量
					if(mStack.back().hasDeclVal(vardecl))
					{
						// 判断变量是否为局部变量
						mStack.back().bindDecl(vardecl, val);
					}
					else
					{
						// 检查是否为全局变量
						assert(gVars.find(vardecl) != gVars.end());
						gVars[vardecl] = val;
					}
				}
				else if(declType->isPointerType())
				{	
					// 左侧为指针类型变量
					mStack.back().bindDecl(vardecl, val);
				}
			}
			else if(isa<ArraySubscriptExpr>(left))
			{
				// 左侧为数组类型
				// 获取数组信息
				ArraySubscriptExpr *arrayexpr = dyn_cast<ArraySubscriptExpr>(left);
				Expr *base = arrayexpr->getBase();
				Expr *idx = arrayexpr->getIdx();
				// 为数组对应位置赋值
				int64_t *ptr = (int64_t *)mStack.back().getStmtVal(base);
				int64_t index = mStack.back().getStmtVal(idx);
				ptr[index] = val;
			}
			else if(isa<UnaryOperator>(left))
			{
				// 左侧为一元操作
				// 现在仅考虑操作符为*
				UnaryOperator *uop = dyn_cast<UnaryOperator>(left);
				Expr *expr = uop->getSubExpr();
				// 赋值
				int64_t *ptr = (int64_t *)mStack.back().getStmtVal(expr);
				*ptr = val;
			}
		}
		else
		{	
			// 其余二元操作实现
			clang::BinaryOperator::Opcode op = bop->getOpcode();
			int64_t left_value = mStack.back().getStmtVal(left);
			int64_t right_value = mStack.back().getStmtVal(right);

			if (left->getType()->isPointerType())
			{
				// 考虑左侧为地址
				right_value = right_value * sizeof(int64_t);
			}
			int64_t result;

			switch (op) {
				case BO_Add:
					result = left_value + right_value;
					break;
				case BO_Sub:
					result = left_value - right_value;
					break;
				case BO_Mul:
					result = left_value * right_value;
					break;
				case BO_Div:
					result = left_value / right_value;
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
				QualType declType = vardecl->getType();
				if(declType->isIntegerType())
				{
					if(vardecl->hasInit())
					{
						// 有初始化
						int64_t val = mStack.back().getStmtVal(vardecl->getInit());
						mStack.back().bindDecl(vardecl, val);
					}
					else
					{
						// 无初始化，则置0
						mStack.back().bindDecl(vardecl, 0);
					}
				}
				else if(declType->isArrayType())
				{
					const ConstantArrayType *arrayDecl = dyn_cast<ConstantArrayType>(declType.getTypePtr());
					int64_t arraySize = arrayDecl->getSize().getSExtValue();
					int64_t *ptr = new int64_t[arraySize];
					// 将数组地址存入mPtrs
					mStack.back().bindDecl(vardecl, (int64_t)ptr);
				}
				else if(declType->isPointerType())
				{
					if(vardecl->hasInit())
					{
						int64_t ptr = (int64_t)mStack.back().getStmtVal(vardecl->getInit());
						mStack.back().bindDecl(vardecl, ptr);
					}
					else
					{
						mStack.back().bindDecl(vardecl, 0);
					}
				}
			}
		}
	}

	void declref(DeclRefExpr *declref)
	{
		mStack.back().setPC(declref);

		// getFoundDecl()可以获得声明
		QualType declType = declref->getType();
		Decl *vardecl = declref->getFoundDecl();
		if (declType->isIntegerType())
		{
			// DeclRefExpr类型为整形
			int64_t val;

			if(mStack.back().hasDeclVal(vardecl))
			{
				// 首先从mVars中查找变量
				val = mStack.back().getDeclVal(vardecl);
			}
			else
			{
				// 不在则查找全局变量
				assert(gVars.find(vardecl) != gVars.end());
				val = gVars[vardecl];
			}
			// 将变量数值存入栈帧
			mStack.back().bindStmt(declref, val);
		}
		else if(declType->isArrayType() || declType->isPointerType())
		{
			int64_t ptr = mStack.back().getDeclVal(vardecl);
			mStack.back().bindStmt(declref, ptr);
		}
	}

	void cast(CastExpr *castexpr)
	{
		mStack.back().setPC(castexpr);
		QualType castType = castexpr->getType();
		Expr *expr = castexpr->getSubExpr();
		if (castType->isIntegerType())
		{
			// ImplicitCastExpr类型为整形
			// 从栈帧中读出子结点映射值，绑定该节点
			// 是因为父节点获取变量值时，只能从子结点的栈帧获得
			int64_t val = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, val);
		}
		else if(castType->isPointerType() && !castType->isFunctionPointerType())
		{
			// ImplicitCastExpr类型为指针
			int64_t ptr = mStack.back().getStmtVal(expr);
			mStack.back().bindStmt(castexpr, ptr);
		}
	}

	/// !TODO Support Function Call
	// 利用返回值判定所调用函数的类型
	int call(CallExpr *callexpr)
	{
		mStack.back().setPC(callexpr);
		int64_t val = 0;
		FunctionDecl *callee = callexpr->getDirectCallee();
		if (callee == mInput)
		{
			llvm::errs() << "Please Input an Integer Value : ";
			//scanf("%d", &val);

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
		else if(callee == mMalloc)
		{
			// 调用了malloc函数
			Expr *size = callexpr->getArg(0);
			val = mStack.back().getStmtVal(size);

			int64_t *ptr = (int64_t *)malloc(val);
			mStack.back().bindStmt(callexpr, (int64_t)ptr);
			return 0;
		}
		else if(callee == mFree)
		{
			Expr *arg = callexpr->getArg(0);
			
			int64_t *ptr = (int64_t *)mStack.back().getStmtVal(arg);
			free(ptr);
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
			// 将函数栈帧压入
			mStack.push_back(funFrame);
			// 调用子函数，返回1
			return 1;
		}
	}
};
