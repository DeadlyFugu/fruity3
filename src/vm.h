#pragma once
#include "common.h"
#include "stack.h"
#include "parser.h"
#include "context.h"
#include "module.h"

typedef struct sVM VM;
typedef struct sExceptionTrace ExceptionTrace;

struct sExceptionTrace {
    ModuleInfo* module;
    // symbol used if module is native, range otherwise
    // todo: these two can probably be an an anonymous union
    SourceRange range;
    const char* symbol;
    ExceptionTrace* next;
};

struct sVM {
    Stack* stack;
    Symbol exSymbol; // 0 when no exception
    const char* exMessage;
    ExceptionTrace* exTraceFirst;
    ExceptionTrace* exTraceLast;
    bool exSourceHasTrace;
    bool fullTrace;
    Context* root;
    Context* context;
    Symbol symSelf, symThis, symOps[20], symExs[5];
    Symbol symKey, symValue, symMessage, symTrace;
    // instead have a context exposed to fruity with module contexts bound within?
    ModuleInfo** modules;
    int moduleCount;
    Context* typeProtos[8];
    Context* refProto, * argProto, * exProto;
};

void VM_startup(VM* vm);
bool VM_eval(VM* vm, Block* block);
bool VM_evalModule(VM* vm, Block* block, Context* ctx);
void VM_dump(VM* vm);
