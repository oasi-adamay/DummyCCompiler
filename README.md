# DummyCCompiler

## 概要
- LLVMのフロントエンド作成のExampleとして作成したLLVMのDummyC用フロントエンド
- DummyCのソースコードをLLVM-IRに変換・出力


## DummyCって？
- 今回のフロントエンド作成用に勝手に定義したC言語のサブセット
- 機能はかなり縮小
- 詳細はdummyC_ebnf.txtを参照


## 基本方針
- 正しい構文を受け入れることを優先
- 誤ったコードに対するエラー処理は深く考えない

## 動作環境
- 下記環境で動作確認してます
  - OS：Windows10
  - LLVM：LLVM 3.8.0
  - Compiler:VC2015

## オリジナル
https://github.com/Kmotiko/DummyCCompiler
https://github.com/cormojs/DummyCCompiler

## 主な変更点
- Path Manager
  パスマネージャが新しくなったらしい。旧バージョンは、legaacyとして残っている。  
  `#include "llvm/PassManager.h"` -> `#include "llvm/IR/LegacyPassManager.h"`
- Linker
  Linkerがオブジェクト化。
  ```
#if 0
	if(llvm::Linker::LinkModules(dest, link_mod.get()))
		return false;
#else
	llvm::Linker linker(*dest);
	if (linker.linkInModule(std::move(link_mod))) {
		return false;
	}
#endif
```

- JIT
  - Windows版LLVM3.8で、JITがうまく動いていない？ので、インタプリタに変更  
  (ExamplesのKaleidoscope-Ch4も動いていない)
  - 関数ポインタを取得する、`EE->getPointerToFunction()`から、JIT,インタプリタ兼用の`EE->runFunction()`に変更
  - JITか、インタプリタかの選択は、`#include "llvm/ExecutionEngine/Interpreter.h"
`か、`#include<llvm/ExecutionEngine/MCJIT.h>
`のどちらをインクルードするかで決まる模様。
  - printf等の関数をインタプリタで使用するためには、`libffi`が必要。
  - `LLVM_ENABLE_FFI`を付けて、LLVM rebuidが必要らしい。
