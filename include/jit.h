/*
 *    jit.h
 *
 *    LLVM related routines
 *
 *    LLST (LLVM Smalltalk or Lo Level Smalltalk) version 0.1
 *
 *    LLST is
 *        Copyright (C) 2012 by Dmitry Kashitsyn   aka Korvin aka Halt <korvin@deeptown.org>
 *        Copyright (C) 2012 by Roman Proskuryakov aka Humbug          <humbug@deeptown.org>
 *
 *    LLST is based on the LittleSmalltalk which is
 *        Copyright (C) 1987-2005 by Timothy A. Budd
 *        Copyright (C) 2007 by Charles R. Childers
 *        Copyright (C) 2005-2007 by Danny Reinhold
 *
 *    Original license of LittleSmalltalk may be found in the LICENSE file.
 *
 *
 *    This file is part of LLST.
 *    LLST is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    LLST is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with LLST.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <types.h>

#include <llvm/Function.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

class MethodCompiler {
private:
    // This structure contains working data which is
    // used during the compilation process. 
    // Add more fields if necessary.
    llvm::Module* m_JITModule;
    llvm::Module* m_TypeModule;
    struct TJITContext {
        TMethod*           method;
        llvm::Function*    function;
        llvm::Module*      module;

        llvm::LLVMContext& llvmContext;

        TJITContext(TMethod* method, llvm::LLVMContext& context)
            : method(method), llvmContext(context) { };
    };

    void doPushInstance(TJITContext& jitContext);
public:
    llvm::Function* compileMethod(TMethod* method);
    MethodCompiler(llvm::Module* JITModule, llvm::Module* TypeModule)
        : m_JITModule(JITModule), m_TypeModule(TypeModule) { }
};

