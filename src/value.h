#pragma once
#include "common.h"

#define FP_HAS_VALUE

// external type
typedef struct sContext Context;
typedef struct sAstNode AstNode;

typedef struct sValue Value;
typedef struct sClosure Closure;
typedef struct sNativeClosure NativeClosure;
typedef struct sStack Stack;
typedef struct sBlob Blob;

typedef enum {
    TYPE_NUMBER,
    TYPE_SYMBOL,
    TYPE_STRING,
    TYPE_ODDBALL,
    TYPE_CONTEXT,
    TYPE_CLOSURE,
    TYPE_LIST,
    TYPE_BLOB
} Type;

struct sValue {
    Type tag;
    union {
        double as_number;
        Symbol as_symbol;
        const char* as_string;
        int as_int;
        Context* as_context;
        Closure* as_closure;
        Stack* as_list;
        Blob* as_blob;
    };
};

struct sClosure {
    AstNode* node;
    Context* binding;
};

struct sNativeClosure {
    void* nativeFn; // type NativeFn
    void* alwaysNull; // to distinguish from Closure
    struct sModuleInfo* module;
    const char* symbolName;
};

struct sBlob {
    const u8* data;
    int size;
};

#define GET_TYPE(v) ((v).tag)
#define GET_NUMBER(v) ((v).as_number)
#define GET_SYMBOL(v) ((v).as_symbol)
#define GET_STRING(v) ((v).as_string)
#define GET_ODDBALL(v) ((v).as_int)
#define GET_CONTEXT(v) ((v).as_context)
#define GET_CLOSURE(v) ((v).as_closure)
#define GET_LIST(v) ((v).as_list)
#define GET_BLOB_SIZE(v) ((v).as_blob->size)
#define GET_BLOB_DATA(v) ((v).as_blob->data)

#define FROM_NUMBER(k) ((Value) { TYPE_NUMBER, .as_number = (k) })
#define FROM_SYMBOL(k) ((Value) { TYPE_SYMBOL, .as_symbol = (k) })
#define FROM_STRING(k) ((Value) { TYPE_STRING, .as_string = (k) })
#define FROM_BOOL(k) ((Value) { TYPE_ODDBALL, .as_int = !(k) })
#define FROM_ODDBALL(k) ((Value) { TYPE_ODDBALL, .as_int = (k) })
#define FROM_CONTEXT(k) ((Value) { TYPE_CONTEXT, .as_context = (k) })
#define FROM_CLOSURE(k) ((Value) { TYPE_CLOSURE, .as_closure = (k) })
#define FROM_LIST(k) ((Value) { TYPE_LIST, .as_list = (k) })

#define VAL_TRUE    ((Value) { TYPE_ODDBALL, .as_int = 0 })
#define VAL_FALSE   ((Value) { TYPE_ODDBALL, .as_int = 1 })
#define VAL_DEFAULT ((Value) { TYPE_ODDBALL, .as_int = 2 })
// todo: maybe rename nil -> none?
#define VAL_NIL     ((Value) { TYPE_ODDBALL, .as_int = 3 })

const char* Value_repr(Value v, int depth);

Value Value_makeBlob(int size, const u8* data);
