#include "module.h"
#include "fruity.h"
#include "vm.h"
#include "context.h"
#include "parser.h"
#include "unistd.h"
#include "sys/stat.h"
#include "dlfcn.h"

// todo: expose properly
extern void raiseInternal(VM* vm, const char* msg);

static bool loadFruityModule(VM* vm, ModuleInfo* info, const char* path);
static bool loadNativeModule(VM* vm, ModuleInfo* info, const char* path);

static const char* modPaths[16] = {
    "/usr/local/lib/fruity",
    "~/.local/lib/fruity"
};
static int nextModPath = 2;

void Module_addPath(const char* path) {
    // todo: allow any amount of mod paths
    assert(nextModPath < 16);
    modPaths[nextModPath++] = path;
}

static bool importCommon(VM* vm, const char* name,
    bool main, bool native, char* path);
extern bool register_builtin(VM* vm, ModuleInfo* module);

bool Module_import(VM* vm, const char* name, bool main) {
    // todo: search cache for only non-rel modules to skip stat
    // for (int i = 0; i < vm->moduleCount; i++) {
    //     if (strcmp(vm->modules[i]->name, name) == 0) {
    //         // todo: have some 'loading' flag to detect circular imports?
    //         Stack_push(vm->stack, vm->modules[i]->value);
    //         return true;
    //     }
    // }

    bool found = false;
    bool native = false;
    char path[1024];
    if (strcmp(name, "builtin") == 0) {
        found = true;
        native = true;
        snprintf(path, 1024, "/__BUILTIN__");
    }
    for (int i = 0; i < nextModPath && !found; i++) {
        snprintf(path, 1024, "%s/%s.fj", modPaths[i], name);
        struct stat s;
        if (stat(path, &s) == 0) {
            found = true;
            break;
        }
        snprintf(path, 1024, "%s/mod%s.so", modPaths[i], name);
        if (stat(path, &s) == 0) {
            found = true;
            native = true;
            break;
        }
    }

    if (!found) {
        raiseInternal(vm, "module not found");
        return false;
    }

    return importCommon(vm, name, main, native, path);
}

bool Module_importRel(VM* vm, const char* name, ModuleInfo* from) {
    if (!from->filename) {
        raiseInternal(vm, "module not found");
        return false;
    }
    const char* base = from->filename;
    const char* end_slash = strrchr(base, '/');
    int base_len = end_slash ? end_slash - base : strlen(base);
    char path[1024];
    snprintf(path, 1024, "%.*s/%s.fj", base_len, base, name);
    struct stat s;
    bool found, native;
    if (stat(path, &s) == 0) {
        found = true;
    } else {
        snprintf(path, 1024, "%.*s/mod%s.so", base_len, base, name);
        if (stat(path, &s) == 0) {
            found = true;
            native = true;
        }
    }
    if (!found) {
        raiseInternal(vm, "module not found");
        return false;
    }
    return importCommon(vm, name, false, native, path);
}

#include <limits.h>
static bool importCommon(VM* vm, const char* name, bool main, bool native, char* path) {
    char rp[PATH_MAX];
    realpath(path, rp);
    for (int i = 0; i < vm->moduleCount; i++) {
        if (strcmp(vm->modules[i]->_realpath, rp) == 0) {
            // todo: have some 'loading' flag to detect circular imports?
            Stack_push(vm->stack, vm->modules[i]->value);
            return true;
        }
    }

    if (vm->modules == NULL) {
        vm->moduleCount = 1;
        vm->modules = GC_MALLOC(sizeof(ModuleInfo*));
    } else {
        vm->moduleCount++;
        vm->modules = GC_REALLOC(vm->modules,
            sizeof(ModuleInfo*) * vm->moduleCount);
    }
    ModuleInfo* info = GC_MALLOC(sizeof(ModuleInfo));
    vm->modules[vm->moduleCount - 1] = info;
    info->name_ = name;
    info->main = main;
    info->native = native;
    info->_realpath = GC_strdup(rp);
    // todo: allow modules to declare themselves as hidden
    if (strcmp(name, "dragon") == 0) info->hideTrace = true;
    // char* filename = strrchr(path, '/'); // todo: need to add 1
    // if (!filename) filename = path;
    char* filename = path;
    if (filename[0] == '.' && filename[1] == '/') filename += 2;

    info->filename = GC_strdup(filename);
    
    if (native) {
        return loadNativeModule(vm, info, path);
    } else {
        return loadFruityModule(vm, info, path);
    }
}

bool Module_fromFile(VM* vm, const char* path, bool main) {
    // todo: this should probably use importCommon too so file is cached
    ModuleInfo* info = GC_MALLOC(sizeof(ModuleInfo));
    *info = (ModuleInfo) {};
    info->name_ = "<file>";
    info->main = main;
    info->native = false;
    info->filename = GC_strdup(path);
    char rp[PATH_MAX];
    realpath(path, rp);
    info->_realpath = GC_strdup(rp);
    return loadFruityModule(vm, info, path);
}

// todo: expose properly
extern bool evalCall(VM* vm, AstNode* caller, Value v, Value* self);

static bool loadFruityModule(VM* vm, ModuleInfo* info, const char* path) {
    FILE* mf = fopen(path, "r");
    if (!mf) {
        raiseInternal(vm, "unable to open file");
        return false;
    }

    // todo: error checking
    fseek(mf, 0, SEEK_END);
    int size = ftell(mf);
    fseek(mf, 0, SEEK_SET);
    char* buf = GC_MALLOC(size + 1);
    fread(buf, 1, size, mf);
    buf[size] = 0;
    fclose(mf);
    info->source = buf;

    Block* block = fpParse(info);
    if (!block) {
        // todo: should convert parse error to exception
        raiseInternal(vm, "syntax error in file");
        return false;
    }
    Context* ctx = Context_create(vm->root);
    info->value = FROM_CONTEXT(ctx);
    // todo: maybe dont bind _module for dragon
    // Context_bind(ctx, Symbol_find("_module", 7), FROM_STRING(info->name));
    if (info->main) Context_bind(ctx, Symbol_find("_main", 5), VAL_TRUE);
    if (!VM_evalModule(vm, block, ctx)) return false;
    Value* exports = Context_get(ctx, Symbol_find("_export", 7));
    if (exports) info->value = *exports;
    Value* hidden = Context_get(ctx, Symbol_find("_hidden", 7));
    if (hidden) info->hideTrace = fpTruthy(*hidden);
    Value* delay = Context_get(ctx, Symbol_find("_delay", 6));
    if (delay) {
        if (!evalCall(vm, NULL, *delay, NULL)) return false;
    }
    Stack_push(vm->stack, info->value);

    return true;
}

static bool loadNativeModule(VM* vm, ModuleInfo* info, const char* path) {
    bool(*fn)(VM*, ModuleInfo*);
    if (strcmp(path, "/__BUILTIN__") == 0) {
        fn = register_builtin;
    } else {
        void* dl = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
        if (!dl) {
            raiseInternal(vm, "unable to open file");
            return false;
        }
        fn = dlsym(dl, "register_module");
        if (!fn) {
            raiseInternal(vm, "cannot find register function");
            return false;
        }
    }
    info->value = VAL_NIL;
    if (!fn(vm, info)) {
        // todo: check exception was raised
        return false;
    }
    Stack_push(vm->stack, info->value);
    return true;
}

void Module_dump(VM* vm) {
    printf("Modules (%d loaded)\n", vm->moduleCount);
    for (int i = 0; i < vm->moduleCount; i++) {
        ModuleInfo* mi = vm->modules[i];
        printf("  %s = %s\n", mi->_realpath, Value_repr(mi->value, 1));
    }
}
