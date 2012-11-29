#ifndef LLST_VM_H_INCLUDED
#define LLST_VM_H_INCLUDED

#include <list>

#include <types.h>
#include <memory.h>
#include <stdlib.h>

class SmalltalkVM {
public:
    enum TExecuteResult {
        returnError = 2,
        returnBadMethod,
        returnReturned,
        returnTimeExpired,
        returnBreak,
        
        returnNoReturn = 255
    }; 
private:
    enum Opcode {
        extended = 0,
        pushInstance,
        pushArgument,    
        pushTemporary,   
        pushLiteral,     
        pushConstant,    
        assignInstance,  
        assignTemporary, 
        markArguments,   
        sendMessage,     
        sendUnary,       
        sendBinary,      
        pushBlock,       
        doPrimitive,     
        doSpecial       
    };
    
    enum Special {
        selfReturn = 1,
        stackReturn,
        blockReturn,
        duplicate,
        popTop,
        branch,
        branchIfTrue,
        branchIfFalse,
        sendToSuper = 11,
        breakpoint = 12
    };
    
    enum {
        nilConst = 10,
        trueConst,
        falseConst
    };
    
    enum TClassID {
        Object,
        Class,
        Method,
        Context,
        Process,
        Array,
        Dictionary,
        Block,
    };
    
    struct TMethodCacheEntry
    {
        TObject* methodName;
        TClass*  receiverClass;
        TMethod* method;
    };
    
    static const unsigned int LOOKUP_CACHE_SIZE = 4096;
    TMethodCacheEntry m_lookupCache[LOOKUP_CACHE_SIZE];
    uint32_t m_cacheHits;
    uint32_t m_cacheMisses;

    // lexicographic comparison of two byte objects
//     int compareSymbols(const TByteObject* left, const TByteObject* right);
    
    // locate the method in the hierarchy of the class
    TMethod* lookupMethod(TSymbol* selector, TClass* klass);
    
    // fast method lookup in the method cache
    TMethod* lookupMethodInCache(TSymbol* selector, TClass* klass);
    
    // flush the method lookup cache
    void flushMethodCache();
    
    void doPushConstant(uint8_t constant, TObjectArray& stack, uint32_t& stackTop);
    
    void doSendMessage(
        TSymbol* selector, 
        TObjectArray& arguments, 
        TContext* context, 
        uint32_t& stackTop);
    
    TObject* doExecutePrimitive(
        uint8_t opcode, 
        TObjectArray& stack, 
        uint32_t& stackTop,
        TProcess& process);
    
    TExecuteResult doDoSpecial(
        TInstruction instruction, 
        TContext* context, 
        uint32_t& stackTop,
        TMethod*& method,
        uint32_t& bytePointer,
        TProcess*& process,
        TObject*& returnedValue);
    
    // The result may be nil if the opcode execution fails (division by zero etc)
    TObject* doSmallInt(
        uint32_t opcode,
        uint32_t leftOperand,
        uint32_t rightOperand);
        
    void failPrimitive(
        TObjectArray& stack,
        uint32_t& stackTop);
    
    void initVariablesFromContext(TContext* context,
                                    TMethod& method,
                                    TByteObject& byteCodes,
                                    uint32_t& bytePointer,
                                    TObjectArray& stack,
                                    uint32_t& stackTop,
                                    TObjectArray& temporaries,
                                    TObjectArray& arguments,
                                    TObjectArray& instanceVariables,
                                    TSymbolArray& literals);
    
    // TODO Think about other memory organization
    std::vector<TObject*> m_rootStack;
    
    Image*          m_image;
    IMemoryManager* m_memoryManager;
    
    void onCollectionOccured();
    
    TObject* newBinaryObject(TClass* klass, size_t slotSize);
    TObject* newOrdinaryObject(TClass* klass, size_t slotSize);
public:    
    TExecuteResult execute(TProcess* process, uint32_t ticks);
    SmalltalkVM(Image* image, IMemoryManager* memoryManager) 
        : m_image(image), m_memoryManager(memoryManager) {}
    
    template<class T> T* newObject(size_t objectSize = 0);
};

template<class T> T* SmalltalkVM::newObject(size_t dataSize /*= 0*/)
{
    // TODO fast access to common classes
    TClass* klass = (TClass*) m_image->getGlobal(T::InstanceClassName());
    if (!klass)
        return (T*) globals.nilObject;
    
    if (T::InstancesAreBinary()) {   
        uint32_t slotSize = sizeof(T) + correctPadding(dataSize);
        return (T*) newBinaryObject(klass, slotSize);
    } else {
        size_t slotSize = sizeof(T) + dataSize * sizeof(T*);
        return (T*) newOrdinaryObject(klass, slotSize);
    }
}

// Specializations of newObject for known types
template<> TObjectArray* SmalltalkVM::newObject<TObjectArray>(size_t dataSize /*= 0*/);
template<> TContext* SmalltalkVM::newObject<TContext>(size_t dataSize /*= 0*/);


#endif
