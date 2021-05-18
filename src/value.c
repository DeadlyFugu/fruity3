#include "value.h"
#include "symbols.h"
#include "context.h"
#include "fruity.h"
#include "stack.h"

#include <stdarg.h>

#ifdef __GNUC__
__attribute__ ((__format__ (__printf__, 1, 0)))
#endif
const char* gc_vsprintf(const char* fmt, va_list args) {
    va_list args2;
    va_copy(args2, args);
    int size = vsnprintf(NULL, 0, fmt, args);
    char* buf = GC_MALLOC(size + 1);
    vsnprintf(buf, size + 1, fmt, args2);
    return buf;
}

#ifdef __GNUC__
__attribute__ ((__format__ (__printf__, 1, 2)))
#endif
const char* gc_sprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const char* result = gc_vsprintf(fmt, args);
    va_end(args);
    return result;
}

const char* Value_repr(Value v, int depth) {
    switch (GET_TYPE(v)) {
        case TYPE_NUMBER: {
            double d = GET_NUMBER(v);
            if (d == 0) return "0";
            else if (d == 1) return "1";
            else {
                // int size = snprintf(NULL, 0, "%lg", d);
                // char* buf = GC_MALLOC(size + 1);
                // snprintf(buf, size + 1, "%lg", d);
                // return buf;
                return gc_sprintf("%lg", d);
            }
        }
        case TYPE_SYMBOL: {
            return Symbol_repr(GET_SYMBOL(v));
        }
        case TYPE_STRING: {
            return gc_sprintf("'%s'", fpStringEscape(GET_STRING(v)));
        }
        case TYPE_ODDBALL: {
            switch(GET_ODDBALL(v)) {
                case 0: return "true";
                case 1: return "false";
                case 2: return "default";
                case 3: return "nil";
            }
        }
        case TYPE_CONTEXT: {
            Context* ctx = GET_CONTEXT(v);
            if (depth == 0 || ctx->count > 6) {
                return gc_sprintf(":{<%d keys>}", ctx->count);
            } else {
                int count = 0;
                int len = 3;
                const char* keys[6];
                const char* values[6];
                for (int i = 0; i < ctx->capacity; i++) {
                    if (!ctx->keys[i]) continue;
                    keys[count] = Symbol_name(ctx->keys[i]);
                    values[count] = Value_repr(ctx->values[i], depth - 1);
                    len += 2 + strlen(keys[count]) + strlen(values[count]);
                    if (count) len += 1;
                    count++;
                }
                char* buf = GC_MALLOC(len + 1);
                buf[0] = 0;
                strcat(buf, ":{");
                for (int i = 0; i < count; i++) {
                    if (i) strcat(buf, " ");
                    strcat(buf, keys[i]);
                    strcat(buf, ": ");
                    strcat(buf, values[i]);
                }
                strcat(buf, "}");
                return buf;
            }
        }
        case TYPE_CLOSURE: {
            // todo: impl
            return "{<not impl>}";
        }
        case TYPE_LIST: {
            Stack* list = GET_LIST(v);
            if (list->next > 6) {
                return gc_sprintf("list(<%d values>)", list->next);
            }
            int len = 6;
            const char* reprs[6];
            for (int i = 0; i < list->next; i++) {
                reprs[i] = Value_repr(list->values[i], 1);
                len += strlen(reprs[i]);
                if (i) len++;
            }
            char* buf = GC_MALLOC(len + 1);
            buf[0] = 0;
            strcat(buf, "list(");
            for (int i = 0; i < list->next; i++) {
                if (i) strcat(buf, " ");
                strcat(buf, reprs[i]);
            }
            strcat(buf, ")");
            return buf;
        }
        case TYPE_BUFFER:
            return "<not impl>";
    }
}
