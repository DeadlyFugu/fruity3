#pragma once
#include "common.h"
#include "value.h"

typedef struct sContext Context;

struct sContext {
    Symbol* keys;
    Value* values;
    Context* parent;
    int capacity; // power of two (nonzero)
    int count:31; // strictly < capacity
    bool lock:1;
};

typedef enum {
    SET_OK,
    SET_UNBOUND,
    SET_LOCKED
} SetResult;

Context* Context_create(Context* parent);
Value* Context_get(Context* ctx, Symbol key);
SetResult Context_set(Context* ctx, Symbol key, Value value);
void Context_bind(Context* ctx, Symbol key, Value value);
void Context_dump(Context* ctx);
