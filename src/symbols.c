#include "symbols.h"

static const char** entries;
static int next;
static int capacity;

Symbol Symbol_find(const char* symbol, int length) {
    for (int i = 1; i < next; i++) {
        const char* entry = entries[i];
        if (strncmp(symbol, entry + 1, length) == 0 &&
            entry[length + 1] == 0) {
            return i;
        }
    }

    if (next == capacity) {
        if (capacity == 0) {
            entries = GC_MALLOC(sizeof(const char**) * 64);
            capacity = 64;
            next = 1;
            entries[0] = "<ZERO>"; // unused
        } else {
            capacity *= 2;
            entries = GC_REALLOC(entries,
                sizeof(const char**) * capacity);
        }
    }
    
    char* repr = GC_MALLOC(length + 2);
    repr[0] = '#';
    repr[length + 1] = 0;
    memcpy(repr + 1, symbol, length);
    entries[next] = repr;
    return next++;
}

const char* Symbol_name(Symbol s) {
    return entries[s] + 1;
}

const char* Symbol_repr(Symbol s) {
    return entries[s];
}
