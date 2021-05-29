
#include "common.h"
#include "value.h"
#include "vm.h"
#include "fruity.h"
#include <gc/gc.h>
#include <stdarg.h>

void* fpAlloc(u32 size) {
    return GC_MALLOC(size);
}

void* fpAllocData(u32 size) {
    return GC_MALLOC_ATOMIC(size);
}

bool fpRequire(VM* vm, const char* sig) {
    assert(0 && "deprecated");
#if 0
    int size = strlen(sig);
    if (vm->stack->next < size) {
        vm->exception = "stack underflow";
        return false;
    }

    int base = vm->stack->next - size;
    for (int i = 0; i < size; i++) {
        Type t = GET_TYPE(vm->stack->values[base + i]);
        switch (sig[i]) {
            case 'd': {
                if (t != TYPE_NUMBER) {
                    vm->exception = "expected number";
                    return false;
                }
            } break;
            case 'i': {
                if (t != TYPE_NUMBER) {
                    vm->exception = "expected integer";
                    return false;
                }
                double d = GET_NUMBER(vm->stack->values[base + i]);
                if (d != (int) d) {
                    vm->exception = "expected integer";
                    return false;
                }
            } break;
            case 's': {
                if (t != TYPE_STRING) {
                    vm->exception = "expected string";
                    return false;
                }
            } break;
            case 'y': {
                if (t != TYPE_SYMBOL) {
                    vm->exception = "expected symbol";
                    return false;
                }
            } break;
            case 'o': {
                if (t != TYPE_ODDBALL) {
                    vm->exception = "expected oddball";
                    return false;
                }
            } break;
            case 'b': {
                if (t != TYPE_ODDBALL) {
                    vm->exception = "expected bool";
                    return false;
                }
                int o = GET_ODDBALL(vm->stack->values[base + i]);
                if (o > 1) {
                    vm->exception = "expected bool";
                    return false;
                }
            } break;
            case 'c': {
                if (t != TYPE_CONTEXT) {
                    vm->exception = "expected context";
                    return false;
                }
            } break;
            case 'f': {
                if (t != TYPE_CLOSURE) {
                    vm->exception = "expected closure";
                    return false;
                }
            } break;
            case 'l': {
                if (t != TYPE_LIST) {
                    vm->exception = "expected list";
                    return false;
                }
            } break;
            case 'u': {
                if (t != TYPE_BUFFER) {
                    vm->exception = "expected buffer";
                    return false;
                }
            } break;
            case '?': break;
            default: {
                assert(0 && "invalid char in sig");
            }
        }
    }
#endif
    return true;
}

// todo: expose properly
extern void raiseUnbound(VM* vm, AstNode* node, Symbol sym);
extern void raiseUnderflow(VM* vm, AstNode* node, int n);
extern void raiseType(VM* vm, AstNode* node, Type type);
extern void raiseInvalid(VM* vm, AstNode* node, const char* msg);
extern void raiseInternal(VM* vm, const char* msg);

static bool extractValue(VM* vm, char kind, Value v, void* dst) {
    Type t = GET_TYPE(v);
    switch(kind) {
        case 'd': {
            if (t != TYPE_NUMBER) {
                raiseType(vm, NULL, TYPE_NUMBER);
                return false;
            }
            *(double*) dst = GET_NUMBER(v);
        } break;
        case 'i': {
            if (t != TYPE_NUMBER) {
                raiseType(vm, NULL, TYPE_NUMBER);
                return false;
            }
            double d = GET_NUMBER(v);
            if (d != (int) d) {
                raiseInvalid(vm, NULL, "expected integer");
                return false;
            }
            *(int*) dst = (int) d;
        } break;
        case 's': {
            if (t != TYPE_STRING) {
                printf("%s\n", Value_repr(v,1));
                raiseType(vm, NULL, TYPE_STRING);
                return false;
            }
            *(const char**) dst = GET_STRING(v);
        } break;
        case 'c': {
            if (t != TYPE_CONTEXT) {
                raiseType(vm, NULL, TYPE_CONTEXT);
                return false;
            }
            *(Context**) dst = GET_CONTEXT(v);
        } break;
        case 'y': {
            if (t != TYPE_SYMBOL) {
                raiseType(vm, NULL, TYPE_SYMBOL);
                return false;
            }
            *(Symbol*) dst = GET_SYMBOL(v);
        } break;
        case 'v': {
            *(Value*) dst = v;
        } break;
        case 'l': {
            if (t != TYPE_LIST) {
                raiseType(vm, NULL, TYPE_LIST);
                return false;
            }
            *(Stack**) dst = GET_LIST(v);
        } break;
        case 'f': {
            if (t != TYPE_CLOSURE) {
                raiseType(vm, NULL, TYPE_CLOSURE);
                return false;
            }
            *(Closure**) dst = GET_CLOSURE(v);
        } break;
        case 'B': {
            if (t != TYPE_BLOB) {
                raiseType(vm, NULL, TYPE_BLOB);
                return false;
            }
            *(Blob**) dst = v.as_blob;
        } break;
        default: {
            assert(0 && "invalid sig char");
        }
    }
    return true;
}

bool fpExtract(VM* vm, const char* sig, ...) {
    // validate stack size
    //int size = strlen(sig);
    int reqstk = 0;
    bool expands = false;
    for (int i = 0; sig[i]; i++) {
        if (sig[i] == '*') {
            assert(sig[i+1] && "invalid sig");
            expands = true; i++;
        } else if (sig[i] == '?') {
            assert(sig[i+1] && "invalid sig");
            reqstk++; i++;
        } else reqstk++;
    }
    int stkSize = vm->stack->next;
    if (expands) {
        if (vm->stack->next < reqstk) {
            raiseUnderflow(vm, NULL, reqstk);
            return false;
        }
        vm->stack->next = 0;
    } else {
        if (vm->stack->next < reqstk) {
            raiseUnderflow(vm, NULL, reqstk);
            return false;
        }
        vm->stack->next -= reqstk;
    }

    va_list args;
    va_start(args, sig);

    int stki = expands ? 0 : stkSize - reqstk;
    for (int i = 0; sig[i]; i++) {
        if (sig[i] == '*') {
            int* count = va_arg(args, int*);
            void** dst = va_arg(args, void**);
            char subType = sig[++i];
            int sigRem = reqstk - i + 1;
            int stkRem = stkSize - stki;
            int width = sizeof(void*);
            switch (subType) {
                case 'd': width = sizeof(double); break;
                case 'i': width = sizeof(int); break;
                case 'y': width = sizeof(Symbol); break;
                case 'o': width = sizeof(int); break;
                case 'b': width = sizeof(bool); break;
                case 'v': width = sizeof(Value); break;
            }
            u8* pBase = (u8*) &vm->stack->values[stki];
            for (int i = 0; i < stkRem - sigRem; i++) {
                Value v = vm->stack->values[stki++];
                if (!extractValue(vm, subType, v, pBase + (i * width))) {
                    goto failure;
                }
            }
            *count = stkRem - sigRem;
            *dst = pBase;
        } else if (sig[i] == '?') {
            bool* isSet = va_arg(args, bool*);
            void* dst = va_arg(args, void*);
            Value v = vm->stack->values[stki++];
            i++;
            if (GET_TYPE(v) == TYPE_ODDBALL && v.as_int == 3) {
                *isSet = false;
            } else {
                *isSet = true;
                if (!extractValue(vm, sig[i], v, dst)) goto failure;
            }
        } else {
            void* dst = va_arg(args, void*);
            Value v = vm->stack->values[stki++];
            if (!extractValue(vm, sig[i], v, dst)) goto failure;
        }
    }

    va_end(args);
    return true;

    failure:
    va_end(args);
    return false;
}

void fpPush(VM* vm, Value v) {
    Stack_push(vm->stack, v);
}

Value fpPop(VM* vm) {
    if (vm->stack->next == 0) {
        assert(0 && "stack underflow");
        exit(-1);
    }
    return Stack_pop(vm->stack);
}

u32 fpStackSize(VM* vm) {
    return vm->stack->next;
}

Symbol fpIntern(const char* ident) {
    return Symbol_find(ident, strlen(ident));
}

Value fpFromDouble(double d) {
    return FROM_NUMBER(d);
}

Value fpFromSymbol(Symbol y) {
    return FROM_SYMBOL(y);
}

Value fpFromString(const char* s) {
    return FROM_STRING(s);
}

Value fpFromBool(bool b) {
    return FROM_BOOL(b);
}

Value fpFromContext(Context* c) {
    return FROM_CONTEXT(c);
}

Value fpFromFunction(ModuleInfo* info, const char* symbol, NativeFn fn) {
    NativeClosure* nc = GC_MALLOC(sizeof(NativeClosure));
    *nc = (NativeClosure) {
        .nativeFn = fn,
        .module = info,
        .symbolName = symbol
    };
    return FROM_CLOSURE((Closure*) nc);
}

double fpToDouble(Value v) {
    return GET_NUMBER(v);
}

Symbol fpToSymbol(Value v) {
    return GET_SYMBOL(v);
}

const char* fpToString(Value v) {
    return GET_STRING(v);
}

Context* fpToContext(Value v) {
    return GET_CONTEXT(v);
}

// todo: currently this is duplicated from vm.c
#include <math.h>
bool fpTruthy(Value v) {
    switch (GET_TYPE(v)) {
        case TYPE_ODDBALL: return
            GET_ODDBALL(v) == 0 || GET_ODDBALL(v) == 2;
        default: return true;
    }
}

Value fpTrue = VAL_TRUE;
Value fpFalse = VAL_FALSE;
Value fpDefault = VAL_DEFAULT;
Value fpNil = VAL_NIL;

const char* fpRepr(Value v) {
    return Value_repr(v, 1);
}

Context* fpContextCreate(Context* parent) {
    return Context_create(parent);
}

void fpContextBind(Context* context, Symbol key, Value value) {
    assert(!context->lock);
    Context_bind(context, key, value);
}

void fpRaiseUnbound(VM* vm, Symbol sym) {
    raiseUnbound(vm, NULL, sym);
}

void fpRaiseUnderflow(VM* vm, int n) {
    raiseUnderflow(vm, NULL, n);
}

void fpRaiseType(VM* vm, int type) {
    raiseType(vm, NULL, type);
}

void fpRaiseInvalid(VM* vm, const char* msg) {
    raiseInvalid(vm, NULL, msg);
}

void fpRaiseInternal(VM* vm, const char* msg) {
    raiseInternal(vm, msg);
}

const char* fpStringEscape(const char* str) {
    int outlen = 0;
    int i;
    for (i = 0; str[i]; i++) {
        if (strchr("'\\\b\f\n\r\t", str[i])) outlen += 2;
        else outlen += 1;
    }
    if (i == outlen) return str;

    char* buf = GC_MALLOC_ATOMIC(outlen + 1);
    int iout = 0;
    for (int iin = 0; str[iin]; iin++) {
        if (strchr("'\\\b\f\n\r\t", str[iin])) {
            buf[iout++] = '\\';
            char c = str[iin];
            switch (str[iin]) {
                case '\b': c = 'b'; break;
                case '\f': c = 'f'; break;
                case '\n': c = 'n'; break;
                case '\r': c = 'r'; break;
                case '\t': c = 't'; break;
            }
            buf[iout++] = c;
        } else {
            buf[iout++] = str[iin];
        }
    }
    assert(iout == outlen);
    buf[iout] = 0;
    return buf;
}

// todo: dont malloc if avoidable?
// todo: error handling for invalid escape sequences?
const char* fpStringUnescape(const char* str, int length) {
    int outlen = 0;
    for (int i = 0; str[i] && i < length; i++) {
        if (str[i] == '\\' && strchr("'\\bfnrt", str[i+1])) i++;
        outlen += 1;
    }
    char* buf = GC_MALLOC_ATOMIC(outlen + 1);
    int iout = 0;
    for (int iin = 0; str[iin] && iin < length; iin++) {
        if (str[iin] == '\\' && strchr("'\\bfnrt", str[iin+1])) {
            iin++;
            char c = str[iin];
            switch (str[iin]) {
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
            }
            buf[iout++] = c;
        } else {
            buf[iout++] = str[iin];
        }
    }
    assert(iout == outlen);
    buf[iout] = 0;
    return buf;
}

void fpModuleSetValue(ModuleInfo* module, Value v) {
    module->value = v;
}

// from value.c
extern const char* gc_vsprintf(const char* fmt, va_list args);
#ifdef __GNUC__
__attribute__ ((__format__ (__printf__, 1, 2)))
#endif
const char* fpSprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const char* result = gc_vsprintf(fmt, args);
    va_end(args);
    return result;
}

void fpBeginList(VM* vm) {
    Stack* old = vm->stack;
    vm->stack = GC_MALLOC(sizeof(Stack));
    *vm->stack = (Stack) { .previous = old };
}

void fpEndList(VM* vm) {
    Stack* list = vm->stack;
    vm->stack = vm->stack->previous;
    Stack_push(vm->stack, FROM_LIST(list));
}

extern bool valueCompare(VM* vm, AstNode* node, Value lhs, Value rhs, int* result);
bool fpValueCompare(VM* vm, Value lhs, Value rhs, int* result) {
    return valueCompare(vm, NULL, lhs, rhs, result);
}
