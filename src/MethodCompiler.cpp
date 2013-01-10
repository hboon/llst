/*
 *    MethodCompiler.cpp
 *
 *    Implementation of MethodCompiler class which is used to
 *    translate smalltalk bytecodes to LLVM IR code
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

#include <jit.h>
#include <vm.h>

using namespace llvm;

Function* MethodCompiler::createFunction(TMethod* method)
{
    std::vector<Type*> methodParams;
    methodParams.push_back(ot.context->getPointerTo());

    FunctionType* functionType = FunctionType::get(
        ot.object->getPointerTo(), // function return value
        methodParams,              // parameters
        false                      // we're not dealing with vararg
    );
    
    std::string functionName = method->klass->name->toString() + ">>" + method->name->toString();
    return cast<Function>( m_JITModule->getOrInsertFunction(functionName, functionType) );
}

void MethodCompiler::writePreamble(TJITContext& jit)
{
    // First argument of every function is a pointer to TContext object
    Value* contextObject = (Value*) (jit.function->arg_begin());
    contextObject->setName("context");
    
    jit.methodObject = jit.builder->CreateStructGEP(contextObject, 1, "method");
    
    Function* objectGetFields = m_TypeModule->getFunction("TObject::getFields()");
    
    // TODO maybe we shuld rewrite arguments[idx] using TArrayObject::getField ?

    Value* argsObjectPtr       = jit.builder->CreateStructGEP(contextObject, 2, "argObjectPtr");
    Value* argsObjectArray     = jit.builder->CreateLoad(argsObjectPtr, "argsObjectArray");
    Value* argsObject          = jit.builder->CreateBitCast(argsObjectArray, ot.object->getPointerTo(), "argsObject");
    jit.arguments       = jit.builder->CreateCall(objectGetFields, argsObject, "arguments");
    
    Value* literalsObjectPtr   = jit.builder->CreateStructGEP(contextObject, 3, "literalsObjectPtr");
    Value* literalsObjectArray = jit.builder->CreateLoad(literalsObjectPtr, "literalsObjectArray");
    Value* literalsObject      = jit.builder->CreateBitCast(literalsObjectArray, ot.object->getPointerTo(), "literalsObject");
    jit.literals        = jit.builder->CreateCall(objectGetFields, literalsObject, "literals");
    
    Value* tempsObjectPtr      = jit.builder->CreateStructGEP(contextObject, 4, "tempsObjectPtr");
    Value* tempsObjectArray    = jit.builder->CreateLoad(tempsObjectPtr, "tempsObjectArray");
    Value* tempsObject         = jit.builder->CreateBitCast(tempsObjectArray, ot.object->getPointerTo(), "tempsObject");
    jit.temporaries     = jit.builder->CreateCall(objectGetFields, tempsObject, "temporaries");
    
    Value* selfObjectPtr       = jit.builder->CreateGEP(jit.arguments, jit.builder->getInt32(0), "selfObjectPtr");
    Value* selfObject          = jit.builder->CreateLoad(selfObjectPtr, "selfObject");
    jit.self            = jit.builder->CreateCall(objectGetFields, selfObject, "self");
}

void MethodCompiler::scanForBranches(TJITContext& jit)
{
    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with
    // target bytecode offset as a key.

    TByteObject& byteCodes   = * jit.method->byteCodes;
    uint32_t     byteCount   = byteCodes.getSize();

    // Processing the method's bytecodes
    while (jit.bytePointer < byteCount) {
        // Decoding the pending instruction (TODO move to a function)
        TInstruction instruction;
        instruction.low = (instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        instruction.high >>= 4;
        if (instruction.high == SmalltalkVM::opExtended) {
            instruction.high = instruction.low;
            instruction.low  = byteCodes[jit.bytePointer++];
        }

        // We're now looking only for branch bytecodes
        if (instruction.high != SmalltalkVM::opDoSpecial)
            continue;

        switch (instruction.low) {
            case SmalltalkVM::branch:
            case SmalltalkVM::branchIfTrue:
            case SmalltalkVM::branchIfFalse: {
                // Loading branch target bytecode offset
                uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
                jit.bytePointer += 2; // skipping the branch offset data
                
                // Creating the referred basic block and inserting it into the function
                // Later it will be filled with instructions and linked to other blocks
                BasicBlock* targetBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "target", jit.function);
                m_targetToBlockMap[targetOffset] = targetBasicBlock;
            } break;
        }
    }
}

Value* MethodCompiler::createArray(TJITContext& jit, uint32_t elementsCount)
{
    // Instantinating new array object
    Value* args[] = { m_globals.arrayClass, jit.builder->getInt32(elementsCount) };
    Value* arrayObject = jit.builder->CreateCall(m_newOrdinaryObjectFunction, args);

    return arrayObject;
}

Function* MethodCompiler::compileMethod(TMethod* method)
{
    TByteObject& byteCodes = * method->byteCodes;
    uint32_t     byteCount = byteCodes.getSize();
    
    TJITContext  jit(method);
    
    // Creating the function named as "Class>>method"
    jit.function = createFunction(method);

    // Creating the basic block and inserting it into the function
    BasicBlock* currentBasicBlock = BasicBlock::Create(m_JITModule->getContext(), "preamble", jit.function);

    jit.builder = new IRBuilder<>(currentBasicBlock);
    
    // Writing the function preamble and initializing
    // commonly used pointers such as method arguments or temporaries
    writePreamble(jit);

    // First analyzing pass. Scans the bytecode for the branch sites and
    // collects branch targets. Creates target basic blocks beforehand.
    // Target blocks are collected in the m_targetToBlockMap map with 
    // target bytecode offset as a key.
    scanForBranches(jit);
    jit.bytePointer = 0; // TODO bytePointer != 0 if we compile Block
    
    // Processing the method's bytecodes
    while (jit.bytePointer < byteCount) {
        uint32_t currentOffset = jit.bytePointer;

        std::map<uint32_t, llvm::BasicBlock*>::iterator iBlock = m_targetToBlockMap.find(currentOffset);
        if (iBlock != m_targetToBlockMap.end()) {
            // Somewhere in the code we have a branch instruction that
            // points to the current offset. We need to end the current
            // basic block and start a new one, linking previous 
            // basic block to a new one.

            BasicBlock* newBlock = iBlock->second;        // Picking a basic block
            jit.builder->CreateBr(newBlock);       // Linking current block to a new one
            jit.builder->SetInsertPoint(newBlock); // and switching builder to a new block
        }
        
        // First of all decoding the pending instruction
        jit.instruction.low = (jit.instruction.high = byteCodes[jit.bytePointer++]) & 0x0F;
        jit.instruction.high >>= 4;
        if (jit.instruction.high == SmalltalkVM::opExtended) {
            jit.instruction.high =  jit.instruction.low;
            jit.instruction.low  =  byteCodes[jit.bytePointer++];
        }

        // Then writing the code
        switch (jit.instruction.high) {
            // TODO Boundary checks against container's real size
            case SmalltalkVM::opPushInstance:    doPushInstance(jit);    break;
            case SmalltalkVM::opPushArgument:    doPushArgument(jit);    break;
            case SmalltalkVM::opPushTemporary:   doPushTemporary(jit);   break;
            case SmalltalkVM::opPushLiteral:     doPushLiteral(jit);     break;
            case SmalltalkVM::opPushConstant:    doPushConstant(jit);    break;
            case SmalltalkVM::opPushBlock:       doPushBlock(jit);       break;
            
            case SmalltalkVM::opAssignTemporary: doAssignTemporary(jit); break;
            case SmalltalkVM::opAssignInstance:  doAssignInstance(jit);  break; // TODO checkRoot

            case SmalltalkVM::opMarkArguments:   doMarkArguments(jit);   break;
            case SmalltalkVM::opSendUnary:       doSendUnary(jit);       break;
            case SmalltalkVM::opSendBinary:      doSendBinary(jit);      break;
            case SmalltalkVM::opSendMessage:     doSendMessage(jit);     break;

            case SmalltalkVM::opDoSpecial:       doSpecial(jit); break;
            
            default:
                fprintf(stderr, "JIT: Invalid opcode %d at offset %d in method %s",
                        jit.instruction.high, jit.bytePointer, method->name->toString().c_str());
                exit(1);
        }
    }

    // TODO Write the function epilogue and do the remaining job

    return jit.function;
}

void MethodCompiler::doPushInstance(TJITContext& jit)
{
    // Self is interprited as object array.
    // Array elements are instance variables

    uint8_t index = jit.instruction.low;
    
    Value* valuePointer     = jit.builder->CreateGEP(jit.self, jit.builder->getInt32(index));
    Value* instanceVariable = jit.builder->CreateLoad(valuePointer);
    jit.pushValue(instanceVariable);
}

void MethodCompiler::doPushArgument(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;

    Value* valuePointer = jit.builder->CreateGEP(jit.arguments, jit.builder->getInt32(index));
    Value* argument     = jit.builder->CreateLoad(valuePointer);
    jit.pushValue(argument);
}

void MethodCompiler::doPushTemporary(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;

    Value* valuePointer = jit.builder->CreateGEP(jit.temporaries, jit.builder->getInt32(index));
    Value* temporary    = jit.builder->CreateLoad(valuePointer);
    jit.pushValue(temporary);
}

void MethodCompiler::doPushLiteral(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;

    Value* valuePointer = jit.builder->CreateGEP(jit.literals, jit.builder->getInt32(index));
    Value* literal      = jit.builder->CreateLoad(valuePointer);
    jit.pushValue(literal);
}

void MethodCompiler::doPushConstant(TJITContext& jit)
{
    const uint8_t constant = jit.instruction.low;
    Value* constantValue   = 0;
    
    switch (constant) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9: {
            Value* integerValue = jit.builder->getInt32(newInteger(constant));
            constantValue       = jit.builder->CreateIntToPtr(integerValue, ot.object);
        } break;
        
        case SmalltalkVM::nilConst:   constantValue = m_globals.nilObject;   break;
        case SmalltalkVM::trueConst:  constantValue = m_globals.trueObject;  break;
        case SmalltalkVM::falseConst: constantValue = m_globals.falseObject; break;
        
        default:
            fprintf(stderr, "JIT: unknown push constant %d\n", constant);
    }
    
    jit.pushValue(constantValue);
}

void MethodCompiler::doPushBlock(TJITContext& jit)
{
    TByteObject& byteCodes = * jit.method->byteCodes;
    uint16_t newBytePointer = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
    jit.bytePointer += 2;
    
    //Value* blockFunction = compileBlock(jit);
    // FIXME We need to push a block object initialized
    //       with the IR code in additional field
    // jit.pushValue(blockFunction);
    jit.bytePointer = newBytePointer;
}

void MethodCompiler::doAssignTemporary(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value* value  = jit.lastValue();
    
    Value* temporaryAddress = jit.builder->CreateGEP(jit.temporaries, jit.builder->getInt32(index));
    jit.builder->CreateStore(value, temporaryAddress);
}

void MethodCompiler::doAssignInstance(TJITContext& jit)
{
    uint8_t index = jit.instruction.low;
    Value* value  = jit.lastValue();
    
    Value* instanceVariableAddress = jit.builder->CreateGEP(jit.self, jit.builder->getInt32(index));
    jit.builder->CreateStore(value, instanceVariableAddress);
    // TODO analog of checkRoot()
}

void MethodCompiler::doMarkArguments(TJITContext& jit)
{
    // Here we need to create the arguments array from the values on the stack
    
    uint8_t argumentsCount = jit.instruction.low;
    
    // FIXME May be we may unroll the arguments array and pass the values directly.
    //       However, in some cases this may lead to additional architectural problems.
    Value* arguments = createArray(jit, argumentsCount);
    
    // Filling object with contents
    uint8_t index = argumentsCount;
    while (index > 0)
        jit.builder->CreateInsertValue(arguments, jit.popValue(), index);
    
    jit.pushValue(arguments);
}

void MethodCompiler::doSendUnary(TJITContext& jit)
{
    Value* value     = jit.popValue();
    Value* condition = 0;
    
    switch ((SmalltalkVM::UnaryOpcode) jit.instruction.low) {
        case SmalltalkVM::isNil:  condition = jit.builder->CreateICmpEQ(value, m_globals.nilObject); break;
        case SmalltalkVM::notNil: condition = jit.builder->CreateICmpNE(value, m_globals.nilObject); break;
        
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendUnary\n", jit.instruction.low);
    }
    
    Value* result = jit.builder->CreateSelect(condition, m_globals.trueObject, m_globals.falseObject);
    jit.pushValue(result);
}

void MethodCompiler::doSendBinary(TJITContext& jit)
{
    // TODO Extract this code into subroutines.
    //      Replace the operation with call to LLVM function
    
    Value* rightValue = jit.popValue();
    Value* leftValue  = jit.popValue();
    
    // Checking if values are both small integers
    Function* isSmallInt  = m_TypeModule->getFunction("isSmallInteger()");
    Value*    rightIsInt  = jit.builder->CreateCall(isSmallInt, rightValue);
    Value*    leftIsInt   = jit.builder->CreateCall(isSmallInt, leftValue);
    Value*    isSmallInts = jit.builder->CreateAnd(rightIsInt, leftIsInt);
    
    BasicBlock* integersBlock   = BasicBlock::Create(m_JITModule->getContext(), "integers",   jit.function);
    BasicBlock* sendBinaryBlock = BasicBlock::Create(m_JITModule->getContext(), "sendBinary", jit.function);
    BasicBlock* resultBlock     = BasicBlock::Create(m_JITModule->getContext(), "result",     jit.function);
    
    // Dpending on the contents we may either do the integer operations
    // directly or create a send message call using operand objects
    jit.builder->CreateCondBr(isSmallInts, integersBlock, sendBinaryBlock);
    
    // Now the integers part
    jit.builder->SetInsertPoint(integersBlock);
    Function* getIntValue = m_TypeModule->getFunction("getIntegerValue()");
    Value*    rightInt    = jit.builder->CreateCall(getIntValue, rightValue);
    Value*    leftInt     = jit.builder->CreateCall(getIntValue, leftValue);
    
    Value* intResult = 0;
    switch (jit.instruction.low) {
        case 0: intResult = jit.builder->CreateICmpSLT(leftInt, rightInt); // operator <
        case 1: intResult = jit.builder->CreateICmpSLE(leftInt, rightInt); // operator <=
        case 2: intResult = jit.builder->CreateAdd(leftInt, rightInt);     // operator +
        default:
            fprintf(stderr, "JIT: Invalid opcode %d passed to sendBinary\n", jit.instruction.low);
    }
    // Jumping out the integersBlock to the value aggregator
    jit.builder->CreateBr(resultBlock);
    
    // Now the sendBinary block
    jit.builder->SetInsertPoint(sendBinaryBlock);
    // We need to create an arguments array and fill it with argument objects
    // Then send the message just like ordinary one
    Value* arguments = createArray(jit, 2);
    jit.builder->CreateInsertValue(arguments, jit.popValue(), 0);
    jit.builder->CreateInsertValue(arguments, jit.popValue(), 1);
    Value* sendMessageResult = jit.builder->CreateCall(m_sendMessageFunction, arguments);
    // Jumping out the sendBinaryBlock to the value aggregator
    jit.builder->CreateBr(resultBlock);
    
    // Now the value aggregator block
    jit.builder->SetInsertPoint(resultBlock);
    // We do not know now which way the program will be executed,
    // so we need to aggregate two possible results one of which
    // will be then selected as a return value
    PHINode* phi = jit.builder->CreatePHI(ot.object, 2);
    phi->addIncoming(intResult, integersBlock);
    phi->addIncoming(sendMessageResult, sendBinaryBlock);
    
    jit.pushValue(phi);
}

void MethodCompiler::doSendMessage(TJITContext& jit)
{
    Value* arguments = jit.popValue();
    Value* result    = jit.builder->CreateCall(m_sendMessageFunction, arguments);
    jit.pushValue(result);
}


Function* MethodCompiler::compileBlock(TJITContext& context)
{
    return 0; // TODO
}

void MethodCompiler::doSpecial(TJITContext& jit)
{
    TByteObject& byteCodes = * jit.method->byteCodes;
    uint8_t opcode = jit.instruction.low;
    
    switch (opcode) {
        case SmalltalkVM::selfReturn:  {
            Value* selfPtr = jit.builder->CreateGEP(jit.arguments, 0);
            Value* self    = jit.builder->CreateLoad(selfPtr);
            jit.builder->CreateRet(self);
        } break;
        case SmalltalkVM::stackReturn: jit.builder->CreateRet(jit.popValue()); break;
        case SmalltalkVM::blockReturn: /* TODO */ break;
        case SmalltalkVM::duplicate:   jit.pushValue(jit.lastValue()); break;
        case SmalltalkVM::popTop:      jit.popValue(); break;

        case SmalltalkVM::branch: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data

            // Finding appropriate branch target 
            // from the previously stored basic blocks
            BasicBlock* target = m_targetToBlockMap[targetOffset];
            jit.builder->CreateBr(target);
        } break;

        case SmalltalkVM::branchIfTrue:
        case SmalltalkVM::branchIfFalse: {
            // Loading branch target bytecode offset
            uint32_t targetOffset  = byteCodes[jit.bytePointer] | (byteCodes[jit.bytePointer+1] << 8);
            jit.bytePointer += 2; // skipping the branch offset data
            
            // Finding appropriate branch target
            // from the previously stored basic blocks
            BasicBlock* targetBlock = m_targetToBlockMap[targetOffset];

            // This is a block that goes right after the branch instruction.
            // If branch condition is not met execution continues right after
            BasicBlock* skipBlock = BasicBlock::Create(m_JITModule->getContext(), "branchSkip", jit.function);

            // Creating condition check
            Value* boolObject = (opcode == SmalltalkVM::branchIfTrue) ? m_globals.trueObject : m_globals.falseObject;
            Value* condition  = jit.popValue();
            Value* boolValue  = jit.builder->CreateICmpEQ(condition, boolObject);
            jit.builder->CreateCondBr(boolValue, targetBlock, skipBlock);

            // Switching to a newly created block
            jit.builder->SetInsertPoint(skipBlock);
        } break;

        case SmalltalkVM::breakpoint:
            // TODO
            break;
    }
}
