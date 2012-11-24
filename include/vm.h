#ifndef LLST_VM_H_INCLUDED
#define LLST_VM_H_INCLUDED

#include <list>
#include <vector>

#include <types.h>
#include "memory.h"

inline uint32_t getIntegerValue(TInteger value) { return (uint32_t) value >> 1; }
inline TInteger newInteger(uint32_t value) { return (value << 1) | 1; }

class Image {
private:
    int    imageFileFD;
    size_t imageFileSize;
    
    void*    imageMap;     // pointer to the map base
    uint8_t* imagePointer; // sliding pointer
    std::vector<TObject*> indirects; // TODO preallocate space
    
    enum TImageRecordType {
        invalidObject = 0,
        ordinaryObject,
        inlineInteger,  // inline 32 bit integer in network byte order
        byteObject,     // 
        previousObject, // link to previously loaded object
        nilObject       // uninitialized (nil) field
    };
    
    uint32_t readWord();
    TObject* readObject();
    bool     openImageFile(const char* fileName);
    void     closeImageFile();
    
    IMemoryAllocator* m_memoryAllocator;
public:
    Image(IMemoryAllocator* allocator) 
        : imageFileFD(-1), imageFileSize(0), 
          imagePointer(0), m_memoryAllocator(allocator) 
    {}
    
    bool loadImage(const char* fileName);
    TObject* getGlobal(const char* name);
    TObject* getGlobal(TSymbol* name);
    
    // GLobal VM objects
};

struct TGlobals {
    TObject* nilObject;
    TObject* trueObject;
    TObject* falseObject;
    TClass*  smallIntClass;
    TClass*  arrayClass;
    TClass*  blockClass;
    TClass*  contextClass;
    TClass*  stringClass;
    TDictionary* globalsObject;
    TMethod* initialMethod;
    TObject* binaryMessages[3]; // NOTE
    TClass*  integerClass;
    TObject* badMethodSymbol;
};

extern TGlobals globals;

class SmalltalkVM {
public:
private:
    enum {
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
        SelfReturn = 1,
        StackReturn,
        BlockReturn,
        Duplicate,
        PopTop,
        Branch,
        BranchIfTrue,
        BranchIfFalse,
        SendToSuper = 11,
        Breakpoint = 12
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
    
    TClass* getRootClass(TClassID id);
    
    enum TExecuteResult {
        returnError = 2,
        returnBadMethod,
        returnReturned,
        returnTimeExpired,
        returnBreak,
        
        returnNoReturn = 255
    }; 
    
    std::list<TObject*> m_rootStack;
    Image m_image;
    
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
    void flushCache();
    
    int execute(TProcess* process, uint32_t ticks);
    void doPushConstant(uint8_t constant, TObjectArray& stack, uint32_t& stackTop);
    
    template<class T> T* newObject(size_t objectSize = 0);
    TObject* newObject(TSymbol* className, size_t objectSize);
    TObject* newObject(TClass* klass);
    
    void doSendMessage(
        TSymbol* selector, 
        TObjectArray& arguments, 
        TContext* context, 
        uint32_t& stackTop);
    
    TObject* doExecutePrimitive(
        uint8_t opcode, 
        TObjectArray& stack, 
        uint32_t& stackTop);
    
    TExecuteResult doDoSpecial(
        TInstruction instruction, 
        TContext* context, 
        uint32_t& stackTop,
        TMethod*& method,
        uint32_t& bytePointer,
        TProcess*& process,
        TObject*& returnedValue);
    
public:
    

};

#endif
