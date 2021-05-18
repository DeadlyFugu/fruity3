#pragma once

// public interface for fruity modules

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef struct sVM VM;
typedef struct sClosure Closure;
typedef struct sContext Context;
typedef struct sModuleInfo ModuleInfo;
typedef bool(*NativeFn)(VM* vm);
typedef u16 Symbol;

#ifndef FP_HAS_VALUE
typedef struct sValue_DUMMY Value;
struct sValue_DUMMY { void* a, * b; };
#endif

// Garbage collected allocation
void* fpAlloc(u32 size);
// Garbage collected allocation containing no pointers
void* fpAllocData(u32 size);

// Require values on stack matching signature
// Throws an exception and returns false on failure
// Signature should be a string of chars matching stack
// e.g. "cy?" for any on top, symbol second, context third
// Character meanings:
// d -> number (Double)
// i -> number (specifically Integer)
// s -> String
// y -> sYmbol
// o -> Oddball (default, nil, true, false)
// b -> Boolean
// c -> Context
// f -> closure (Function)
// l -> List
// u -> bUffer
// v -> any type
// ?X -> type X or nil (additional bool field for if set)
// *X -> 0 or more repeats of X (where X is another type)
// ? and * cannot be combined
// todo: remove this, jsut have extract
bool fpRequire(VM* vm, const char* sig);

bool fpExtract(VM* vm, const char* sig, ...);

// todo: fpUnpack which acts like require but also stores values?

// Push value
void fpPush(VM* vm, Value v);
// Pop value, terminates program on stack underflow
Value fpPop(VM* vm);
// Get stack size
u32 fpStackSize(VM* vm);

// Find symbol
Symbol fpIntern(const char* ident);

// Convert double -> Value
Value fpFromDouble(double d);
// Convert symbol -> Value
Value fpFromSymbol(Symbol y);
// Convert double -> Value
Value fpFromString(const char* s);
// Convert bool -> Value
Value fpFromBool(bool b);
// Convert context -> Value
Value fpFromContext(Context* c);
// Convert native fn -> Value
Value fpFromFunction(ModuleInfo* info, const char* symbol, NativeFn fn);

// Convert Value -> double
double fpToDouble(Value v);
// Convert Value -> symbol
Symbol fpToSymbol(Value v);
// Convert Value -> double
const char* fpToString(Value v);
// Convert Value -> context
Context* fpToContext(Value v);

// Returns whether a given value is considered 'true'
bool fpTruthy(Value v);

extern Value fpTrue, fpFalse, fpDefault, fpNil;

// Returns a string representation of a given value
const char* fpRepr(Value v);

// Create a new context
Context* fpContextCreate(Context* parent);
// Bind a value in a given context
void fpContextBind(Context* context, Symbol key, Value value);

void fpRaiseUnbound(VM* vm, Symbol sym);
void fpRaiseUnderflow(VM* vm, int n);
void fpRaiseType(VM* vm, int type);
void fpRaiseInvalid(VM* vm, const char* msg);
void fpRaiseInternal(VM* vm, const char* msg);

const char* fpStringEscape(const char* str);
const char* fpStringUnescape(const char* str, int length);

// todo: expose ModuleInfo internals?
void fpModuleSetValue(ModuleInfo* module, Value v);

#ifdef __GNUC__
__attribute__ ((__format__ (__printf__, 1, 2)))
#endif
const char* fpSprintf(const char* fmt, ...);

void fpBeginList(VM* vm);
void fpEndList(VM* vm);

bool fpValueCompare(VM* vm, Value lhs, Value rhs, int* result);
