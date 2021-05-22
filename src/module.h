#pragma once
#include "common.h"
#include "value.h"

typedef struct sModuleInfo ModuleInfo;
typedef struct sVM VM;
typedef struct sContext Context;

typedef bool(*NativeFn)(VM* vm);

struct sModuleInfo {
    const char* name;
    const char* source;
    // Context* ctx;
    Value value;
    bool main;
    bool native;
    bool hideTrace;
    const char* filename;
};

void Module_addPath(const char* path);
bool Module_import(VM* vm, const char* name, bool main);
bool Module_fromFile(VM* vm, const char* path, bool main);
// ModuleInfo* Module_fromString(VM* vm,
//     const char* label, const char* source);
void Module_dump(VM* vm);
