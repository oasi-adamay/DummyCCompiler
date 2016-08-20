#include "codegen.hpp"

/**
  * コンストラクタ
  */
CodeGen::CodeGen(){
	Builder = new llvm::IRBuilder<>(llvm::getGlobalContext());
	Mod = NULL;
}

/**
  * デストラクタ
  */
CodeGen::~CodeGen(){
	SAFE_DELETE(Builder);
	SAFE_DELETE(Mod);
}


/**
  * コード生成実行
  * @param  TranslationUnitAST　Module名(入力ファイル名)
  * @return 成功時：true　失敗時:false
  */
bool CodeGen::doCodeGen(TranslationUnitAST &tunit, std::string name, 
		std::string link_file, bool with_jit=false){

	if(!generateTranslationUnit(tunit, name)){
		return false;
	}

	//Mod->dump();

	//LinkFileの指定があったらModuleをリンク
	if( !link_file.empty() && !linkModule(Mod, link_file) )
		return false;

	//JITのフラグが立っていたらJIT
	if(with_jit){
#if 0
		llvm::ExecutionEngine *EE = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(Mod)).create();
		llvm::EngineBuilder(std::unique_ptr<llvm::Module>(Mod)).create();
		llvm::Function *F;
		if(!(F=Mod->getFunction("main")))
			return false;

		int (*fp)() = (int (*)())EE->getPointerToFunction(F);
		fprintf(stderr,"%d\n",fp());
#else

		llvm::ExecutionEngine *EE = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(Mod)).create();
		llvm::Function *F;
		if (!(F = Mod->getFunction("main")))
			return false;

		// Call the `main` function with no arguments:
		std::vector<llvm::GenericValue> noargs;
		llvm::GenericValue gv = EE->runFunction(F, noargs);
		int ret = gv.IntVal.getSExtValue();
		std::string id = Mod->getModuleIdentifier();
		fprintf(stderr, "Exit with code:%d (%s)\n", ret, id.c_str());
#endif


	}

	return true;
}


/**
  * Module取得
  */
llvm::Module &CodeGen::getModule(){
	if(Mod)
		return *Mod;
	else
		return *(new llvm::Module("null", llvm::getGlobalContext()));
}


/**
  * Module生成メソッド
  * @param  TranslationUnitAST Module名(入力ファイル名)
  * @return 成功時：true　失敗時：false　
  */
bool CodeGen::generateTranslationUnit(TranslationUnitAST &tunit, std::string name){
	Mod = new llvm::Module(name, llvm::getGlobalContext());
	//funtion declaration
	for(int i=0; ; i++){
		PrototypeAST *proto=tunit.getPrototype(i);
		if(!proto)
			break;
		else if(!generatePrototype(proto, Mod)){
			SAFE_DELETE(Mod);
			return false;
		}
	}

	//function definition
	for(int i=0; ; i++){
		FunctionAST *func=tunit.getFunction(i);
		if(!func)
			break;
		else if(!(generateFunctionDefinition(func, Mod))){
			SAFE_DELETE(Mod);
			return false;
		}
	}

	return true;
}


/**
  * 関数定義生成メソッド
  * @param  FunctionAST Module
  * @return 生成したFunctionのポインタ
  */
llvm::Function *CodeGen::generateFunctionDefinition(FunctionAST *func_ast,
		llvm::Module *mod){
	llvm::Function *func=generatePrototype(func_ast->getPrototype(), mod);
	if(!func){
		return NULL;
	}
	CurFunc = func;
	llvm::BasicBlock *bblock=llvm::BasicBlock::Create(llvm::getGlobalContext(),
									"entry",func);
	Builder->SetInsertPoint(bblock);
	generateFunctionStatement(func_ast->getBody());

	// Validate the generated code, checking for consistency.
	// 矛盾が無いか、生成されたコードを検証する。
	llvm::verifyFunction(*func);

	return func;
}


/**
  * 関数宣言生成メソッド
  * @param  PrototypeAST, Module
  * @return 生成したFunctionのポインタ
  */
llvm::Function *CodeGen::generatePrototype(PrototypeAST *proto, llvm::Module *mod){
	//already declared?
	llvm::Function *func=mod->getFunction(proto->getName());
	if(func){
		if(func->arg_size()==proto->getParamNum() && 
				func->empty()){
			return func;
		}else{
			fprintf(stderr, "error::function %s is redefined",proto->getName().c_str());
			return NULL;
		}
	}

	//create arg_types
	std::vector<llvm::Type*> int_types(proto->getParamNum(),
								llvm::Type::getInt32Ty(llvm::getGlobalContext()));

	//create func type
	llvm::FunctionType *func_type = llvm::FunctionType::get(
							llvm::Type::getInt32Ty(llvm::getGlobalContext()),
							int_types,false
							);
	//create function
	func=llvm::Function::Create(func_type, 
							llvm::Function::ExternalLinkage,
							proto->getName(),
							mod);

	//set names
	llvm::Function::arg_iterator arg_iter=func->arg_begin();
	for(int i=0; i<proto->getParamNum(); i++){
		arg_iter->setName(proto->getParamName(i).append("_arg"));
		++arg_iter;
	}

	return func;
}


/**
  * 関数生成メソッド
  * 変数宣言、ステートメントの順に生成　
  * @param  FunctionStmtAST
  * @return 最後に生成したValueのポインタ
  */
llvm::Value *CodeGen::generateFunctionStatement(FunctionStmtAST *func_stmt){
	//insert variable decls
	llvm::Value *v=NULL;
	for(int i=0; ; i++){
		//最後まで見たら終了
		if(!func_stmt->getVariableDecl(i))
			break;

		//create alloca
		v = generate(func_stmt->getVariableDecl(i));
	}

	//insert expr statement
	BaseAST *stmt;
	for(int i=0; ; i++){
		stmt=func_stmt->getStatement(i);
		if(!stmt)
			break;
		else if(!llvm::isa<NullExprAST>(stmt))
			v=generate(stmt);
	}

	return v;
}


/**
  * 変数宣言(alloca命令)生成メソッド
  * @param VariableDeclAST
  * @return 生成したValueのポインタ
  */
llvm::Value *CodeGen::generateVariableDeclaration(VariableDeclAST *vdecl){
	//create alloca
	llvm::AllocaInst *alloca=Builder->CreateAlloca(
			llvm::Type::getInt32Ty(llvm::getGlobalContext()),
			0,
			vdecl->getName());

	//if args alloca
	if(vdecl->getType()==VariableDeclAST::param){
		//store args
		llvm::ValueSymbolTable &vs_table = CurFunc->getValueSymbolTable();
		Builder->CreateStore(vs_table.lookup(vdecl->getName().append("_arg")), alloca);
	}
	//ValueMap[vdecl->getName()]=alloca;
	return alloca;
}




/**
  * 二項演算生成メソッド
  * @param  JumpStmtAST
  * @return 生成したValueのポインタ
  */
llvm::Value *CodeGen::generateBinaryExpression(BinaryExprAST *bin_expr){
	BaseAST *lhs=bin_expr->getLHS();
	BaseAST *rhs=bin_expr->getRHS();

	llvm::Value *lhs_v;
	llvm::Value *rhs_v;

	//assignment
	if(bin_expr->getOp()=="="){
		//lhs is variable
		VariableAST *lhs_var=llvm::dyn_cast<VariableAST>(lhs);
		llvm::ValueSymbolTable &vs_table = CurFunc->getValueSymbolTable();
		lhs_v = vs_table.lookup(lhs_var->getName());

	//other operand
	}else{
		lhs_v = generate(lhs);
	}


	//create rhs value
	rhs_v = generate(rhs);
	
	
	if(bin_expr->getOp()=="="){
		//store
		return Builder->CreateStore(rhs_v, lhs_v);
	}
	else if(bin_expr->getOp()=="+"){
		//add
		return Builder->CreateAdd(lhs_v, rhs_v, "add_tmp");
	}
	else if(bin_expr->getOp()=="-"){
		//sub
		return Builder->CreateSub(lhs_v, rhs_v, "sub_tmp");
	}
	else if(bin_expr->getOp()=="*"){
		//mul
		return Builder->CreateMul(lhs_v, rhs_v, "mul_tmp");
	}
	else if(bin_expr->getOp()=="/"){
		//div
		return Builder->CreateSDiv(lhs_v, rhs_v, "div_tmp");
	}
	else if(bin_expr->getOp()=="=="){
		//cmpeq
		llvm::Value *tmp = Builder->CreateICmpEQ(lhs_v, rhs_v, "cmpeq_tmp");
		// Convert bool 0/1 to int 0 or 1
		return Builder->CreateZExt(tmp, llvm::Type::getInt32Ty(llvm::getGlobalContext()),"bool_tmp");
	}
	else if (bin_expr->getOp() == "!=") {
		//cmpne
		llvm::Value *tmp = Builder->CreateICmpNE(lhs_v, rhs_v, "cmpne_tmp");
		return Builder->CreateZExt(tmp, llvm::Type::getInt32Ty(llvm::getGlobalContext()), "bool_tmp");
	}
	else if (bin_expr->getOp() == "<") {
		//cmplt
		llvm::Value *tmp = Builder->CreateICmpSLT(lhs_v, rhs_v, "cmplt_tmp");
		return Builder->CreateZExt(tmp, llvm::Type::getInt32Ty(llvm::getGlobalContext()), "bool_tmp");
	}
	else if (bin_expr->getOp() == "<=") {
		//cmple
		llvm::Value *tmp = Builder->CreateICmpSLE(lhs_v, rhs_v, "cmple_tmp");
		return Builder->CreateZExt(tmp, llvm::Type::getInt32Ty(llvm::getGlobalContext()), "bool_tmp");
	}
	else if (bin_expr->getOp() == ">") {
		//cmpgt
		llvm::Value *tmp = Builder->CreateICmpSGT(lhs_v, rhs_v, "cmpgt_tmp");
		return Builder->CreateZExt(tmp, llvm::Type::getInt32Ty(llvm::getGlobalContext()), "bool_tmp");
	}
	else if (bin_expr->getOp() == ">=") {
		//cmpge
		llvm::Value *tmp = Builder->CreateICmpSGE(lhs_v, rhs_v, "cmple_tmp");
		return Builder->CreateZExt(tmp, llvm::Type::getInt32Ty(llvm::getGlobalContext()), "bool_tmp");
	}


	return nullptr;
}


/**
  * 関数呼び出し(Call命令)生成メソッド
  * @param CallExprAST
  * @return 生成したValueのポインタ　
  */
llvm::Value *CodeGen::generateCallExpression(CallExprAST *call_expr){
	std::vector<llvm::Value*> arg_vec;
	BaseAST *arg;
	llvm::Value *arg_v;
	llvm::ValueSymbolTable &vs_table = CurFunc->getValueSymbolTable();
	for(int i=0; ; i++){
		if(!(arg=call_expr->getArgs(i)))
			break;

		arg_v = generate(arg);

		//代入の時はLoad命令を追加
		if (llvm::isa<BinaryExprAST>(arg)) {
			BinaryExprAST *bin_expr = llvm::dyn_cast<BinaryExprAST>(arg);
			if (bin_expr->getOp() == "=") {
				VariableAST *var = llvm::dyn_cast<VariableAST>(bin_expr->getLHS());
				arg_v = Builder->CreateLoad(vs_table.lookup(var->getName()), "arg_val");
			}
		}

		arg_vec.push_back(arg_v);
	}
	return Builder->CreateCall( Mod->getFunction(call_expr->getCallee()),
										arg_vec,"call_tmp" );
}


/**
  * ジャンプ(今回はreturn命令のみ)生成メソッド
  * @param  JumpStmtAST
  * @return 生成したValueのポインタ
  */
llvm::Value *CodeGen::generateJumpStatement(JumpStmtAST *jump_stmt){
	BaseAST *expr=jump_stmt->getExpr();
	llvm::Value *ret_v;
	ret_v = generate(expr);
	Builder->CreateRet(ret_v);
	return ret_v;
}

/**
* if else 生成メソッド
* @param  IfStmtAST
* @return 生成したValueのポインタ
*/
llvm::Value *CodeGen::generateIfStatement(IfStmtAST *if_stmt) {
	BaseAST *cond_expr = if_stmt->getCond();
	BaseAST *then_stmt = if_stmt->getThen();
	BaseAST *else_stmt = if_stmt->getElse();

	llvm::Value *cond_v;
	cond_v = generate(cond_expr);
	if (!cond_v) return NULL;

	// 条件を0と比較することによって真偽値に変換する。
	cond_v = Builder->CreateICmpNE(	cond_v, Builder->getInt32(0), "if.cond");

	// 現在構築中の関数オブジェクトを取得する。
	llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

	// thenとelseの場合のためのブロックを生成する。
	// thenブロックを関数の最後に挿入する。
	llvm::BasicBlock *ThenBB =	llvm::BasicBlock::Create(llvm::getGlobalContext(), "if.then", TheFunction);
	llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "if.else");
	llvm::BasicBlock *EndBB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "if.end");

	//ブロックが生成されたら、どちらのブロックを選ぶかを決めるための条件分岐を生成することが出来る。
	Builder->CreateCondBr(cond_v, ThenBB, ElseBB);

	// then値を発行する。
	// 条件分岐が挿入されたあと、Builderにthenブロックへ挿入させるようにする。
	Builder->SetInsertPoint(ThenBB);

	// 挿入点がセットされたら、ASTからthenのCodegenを再帰的に実行する。
	if (else_stmt) {
		llvm::Value *then_v = generate(then_stmt);
		if (!then_v) return nullptr;
	}

	// thenブロックを仕上げるために、無条件の分岐（br命令）をendブロックに生成する。
	Builder->CreateBr(EndBB);

	// thenのCodegenは、現在のブロックを変更し、phiのためにThenBBを更新しうる。
	ThenBB = Builder->GetInsertBlock();

	// elseブロックを発行する。
	if (else_stmt) {
		TheFunction->getBasicBlockList().push_back(ElseBB);
		Builder->SetInsertPoint(ElseBB);
		llvm::Value *else_v = generate(else_stmt);
		if (!else_v) return nullptr;
	}


	// elseブロックを仕上げるために、無条件の分岐（br命令）をmergeブロックに生成する。
	Builder->CreateBr(EndBB);

	// elseのCodegenは、現在のブロックを変更し、phiのためにElseBBを更新しうる。
	ElseBB = Builder->GetInsertBlock();

	// 分岐を合流させるコード（end code）
	TheFunction->getBasicBlockList().push_back(EndBB);
	Builder->SetInsertPoint(EndBB);

#if 1
	//TheFunction->dump();
	return nullptr;
#else
	llvm::PHINode *PN =
		Builder->CreatePHI(llvm::Type::getInt32Ty(llvm::getGlobalContext()), 2, "iftmp");

	PN->addIncoming(then_v, ThenBB);
	PN->addIncoming(else_v, ElseBB);

	TheFunction->dump();

	return PN;
#endif
}



/**
  * 変数参照(load命令)生成メソッド
  * @param VariableAST
  * @return  生成したValueのポインタ
  */
llvm::Value *CodeGen::generateVariable(VariableAST *var){
	llvm::ValueSymbolTable &vs_table = CurFunc->getValueSymbolTable();
	return Builder->CreateLoad(vs_table.lookup(var->getName()), "var_tmp");
}


llvm::Value *CodeGen::generateNumber(int value){
	return llvm::ConstantInt::get(
			llvm::Type::getInt32Ty(llvm::getGlobalContext()),
			value);
}

/**
* Valueを返す、全てのAST生成メソッド　wrapper
* @param ast
* @return  生成したValueのポインタ
*/
llvm::Value *CodeGen::generate(BaseAST *ast) {

	llvm::Value *val = nullptr;

	//Binary?
	if(llvm::isa<BinaryExprAST>(ast)){
		val =generateBinaryExpression(llvm::dyn_cast<BinaryExprAST>(ast));
    }
	//Variable?
	else if(llvm::isa<VariableAST>(ast)){
		val =generateVariable(llvm::dyn_cast<VariableAST>(ast));
    }
	//Number?
	else if(llvm::isa<NumberAST>(ast)){
		NumberAST *num=llvm::dyn_cast<NumberAST>(ast);
		val =generateNumber(num->getNumberValue());
	}
	//Call ?
	else if (llvm::isa<CallExprAST>(ast)) {
		val = generateCallExpression(llvm::dyn_cast<CallExprAST>(ast));
	}
	//Jump ?
	else if (llvm::isa<JumpStmtAST>(ast)) {
		val = generateJumpStatement(llvm::dyn_cast<JumpStmtAST>(ast));
	}
	//if-else ?
	else if (llvm::isa<IfStmtAST>(ast)) {
		val = generateIfStatement(llvm::dyn_cast<IfStmtAST>(ast));
	}
	// VariableDeclaration ? 
	else if (llvm::isa<VariableDeclAST>(ast)) {
		val = generateVariableDeclaration(llvm::dyn_cast<VariableDeclAST>(ast));
	}


	return val;



}



bool CodeGen::linkModule(llvm::Module *dest, std::string file_name){
	llvm::SMDiagnostic err;
	std::unique_ptr<llvm::Module> link_mod = llvm::parseIRFile(file_name, err, llvm::getGlobalContext());
	if(!link_mod)
		return false;

#if 0
	if(llvm::Linker::LinkModules(dest, link_mod.get()))
		return false;
#else
	llvm::Linker linker(*dest);
	if (linker.linkInModule(std::move(link_mod))) {
		return false;
	}
#endif


	return true;
}
