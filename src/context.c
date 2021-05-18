#include "context.h"
#include "symbols.h"

#define START_CAP 8
#define HASH(k) ((k) ^ 0xF93A)

Context* Context_create(Context* parent) {
    Context* ctx = GC_MALLOC(sizeof(Context));
    *ctx = (Context) { .parent = parent, .capacity = START_CAP };
    ctx->keys = GC_MALLOC((sizeof(Symbol) + sizeof(Value)) * START_CAP);
    ctx->values = (Value*) &ctx->keys[START_CAP];
    memset(ctx->keys, 0, sizeof(Symbol) * START_CAP);
    return ctx;
}

Value* Context_get(Context* ctx, Symbol key) {
    int index = HASH(key) & (ctx->capacity - 1); // key % capacity
    // note: infinite loop if count == capacity!
    while (ctx->keys[index]) {
        if (ctx->keys[index] == key) return &ctx->values[index];
        index = (index + 1) & (ctx->capacity - 1);
    }
    return ctx->parent ? Context_get(ctx->parent, key) : NULL;
}

bool Context_set(Context* ctx, Symbol key, Value value) {
    int index = HASH(key) & (ctx->capacity - 1); // key % capacity
    // note: infinite loop if count == capacity!
    while (ctx->keys[index]) {
        if (ctx->keys[index] == key) {
            ctx->values[index] = value;
            return true;
        }
        index = (index + 1) & (ctx->capacity - 1);
    }
    return ctx->parent ? Context_set(ctx->parent, key, value) : false;
}

void Context_bind(Context* ctx, Symbol key, Value value) {
    // set key if already present
    int index = HASH(key) & (ctx->capacity - 1); // key % capacity
    // note: infinite loop if count == capacity!
    while (ctx->keys[index]) {
        if (ctx->keys[index] == key) {
            ctx->values[index] = value;
            return;
        }
        index = (index + 1) & (ctx->capacity - 1);
    }

    // resize if needed
    // todo: resize earlier (e.g. 75% full? profile to find sweet spot)
    if (++ctx->count == ctx->capacity) {
        int oldCap = ctx->capacity;
        Symbol* oldKeys = ctx->keys;
        Value* oldValues = ctx->values;
        ctx->capacity *= 2;
        // todo: can this be done with realloc instead?
        ctx->keys = GC_MALLOC((sizeof(Symbol) + sizeof(Value)) *
            ctx->capacity);
        ctx->values = (Value*) &ctx->keys[ctx->capacity];
        memset(ctx->keys, 0, sizeof(Symbol) * ctx->capacity);

        // copy across old values
        for (int i = 0; i < oldCap; i++) {
            if (!oldKeys[i]) continue;
            int newIndex = HASH(oldKeys[i]) & (ctx->capacity - 1);
            while (ctx->keys[newIndex]) {
                newIndex = (newIndex + 1) & (ctx->capacity - 1);
            }
            ctx->keys[newIndex] = oldKeys[i];
            ctx->values[newIndex] = oldValues[i];
        }

        // find new index for insertion
        index = HASH(key) & (ctx->capacity - 1);
        while (ctx->keys[index]) {
            index = (index + 1) & (ctx->capacity - 1);
        }
    }

    // insert key
    ctx->keys[index] = key;
    ctx->values[index] = value;
}

void Context_dump(Context* ctx) {
    printf("Context(count: %d, capacity: %d)\n",
        ctx->count, ctx->capacity);
    for (int i = 0; i < ctx->capacity; i++) {
        if (ctx->keys[i])
            printf("  entry[%d] = %s -> %s\n", i,
                Symbol_repr(ctx->keys[i]), Value_repr(ctx->values[i], 1));
        else
            printf("  entry[%d] = <empty>\n", i);
    }
}
// 1 2 3 4 5 6 7 8 9 10 >>b >>c >>d >>e >>f >>g >>h >>i >>j >>k $b $c $d $e $f $g $h $i $j $k