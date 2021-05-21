#include "../common.h"
#include "../vm.h"
#include "../fruity.h"

#include <errno.h>
#include <gc/gc.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <readline/readline.h>
#include <readline/history.h>

bool builtin_stksize(VM* vm) {
    fpPush(vm, fpFromDouble(fpStackSize(vm)));
    return true;
}

bool builtin_reverse(VM* vm) {
    Stack_reverse(vm->stack);
    return true;
}

bool builtin_dump(VM* vm) {
    Stack* stk = vm->stack;
    for (int level = 0; stk; stk = stk->previous, level--) {
        printf("Stack %d (size %d, capacity %d)\n",
            level, stk->next, stk->capacity);
        for (int i = stk->next-1; i >= 0; i--) {
            printf("  [%d] = %s\n", i, fpRepr(stk->values[i]));
        }
    }
    return true;

    // printf("Stack = {");
    // for (int i = 0; i < vm->stack->next; i++) {
    //     if (i != 0) printf(", ");
    //     printf("%s", fpRepr(vm->stack->values[i]));
    // }
    // printf("}\n");
    return true;
}

bool builtin_ctxdump(VM* vm) {
    Context* c;
    if (!fpExtract(vm, "c", &c)) return false;
    Context_dump(c);
    return true;
}

bool builtin_valstr(VM* vm) {
    Value v;
    if (!fpExtract(vm, "v", &v)) return false;
    fpPush(vm, fpFromString(fpRepr(v)));
    return true;
}

bool builtin_getv(VM* vm) {
    Context* c;
    Symbol s;
    // todo: string-or-symbol char in extract?
    if (!fpExtract(vm, "cy", &c, &s)) return false;
    Value* pv = Context_get(c, s);
    if (pv) fpPush(vm, *pv);
    else {
        fpRaiseUnbound(vm, s);
        return false;
    }
    return true;
}

bool builtin_setv(VM* vm) {
    Context* c;
    Symbol s;
    Value v;
    // todo: string-or-symbol char in extract?
    if (!fpExtract(vm, "cyv", &c, &s, &v)) return false;
    if (!Context_set(c, s, v)) {
        fpRaiseUnbound(vm, s);
        return false;
    }
    return true;
}

bool builtin_bindv(VM* vm) {
    Context* c;
    Symbol s;
    Value v;
    // todo: string-or-symbol char in extract?
    if (!fpExtract(vm, "cyv", &c, &s, &v)) return false;
    Context_bind(c, s, v);
    return true;
}

bool builtin_hasv(VM* vm) {
    Context* c;
    Symbol s;
    // todo: string-or-symbol char in extract?
    if (!fpExtract(vm, "cy", &c, &s)) return false;
    Value* pv = Context_get(c, s);
    fpPush(vm, pv ? VAL_TRUE : VAL_FALSE);
    return true;
}

bool builtin_lsv(VM* vm) {
    Context* c;
    // todo: string-or-symbol char in extract?
    if (!fpExtract(vm, "c", &c)) return false;
    for (int i = 0; i < c->capacity; i++) {
        if (c->keys[i]) fpPush(vm, FROM_SYMBOL(c->keys[i]));
    }
    return true;
}

bool builtin_type(VM* vm) {
    Value v;
    // todo: string-or-symbol char in extract?
    if (!fpExtract(vm, "v", &v)) return false;
    // todo: store interned type symbols on vm
    switch (GET_TYPE(v)) {
        case TYPE_NUMBER: fpPush(vm, FROM_SYMBOL(fpIntern("Number"))); break;
        case TYPE_SYMBOL: fpPush(vm, FROM_SYMBOL(fpIntern("Symbol"))); break;
        case TYPE_STRING: fpPush(vm, FROM_SYMBOL(fpIntern("String"))); break;
        case TYPE_ODDBALL: fpPush(vm, FROM_SYMBOL(fpIntern("Oddball"))); break;
        case TYPE_CONTEXT: fpPush(vm, FROM_SYMBOL(fpIntern("Context"))); break;
        case TYPE_CLOSURE: fpPush(vm, FROM_SYMBOL(fpIntern("Closure"))); break;
        case TYPE_LIST: fpPush(vm, FROM_SYMBOL(fpIntern("List"))); break;
        case TYPE_BLOB: fpPush(vm, FROM_SYMBOL(fpIntern("Blob"))); break;
    }
    return true;
}

bool builtin_getp(VM* vm) {
    Value v;
    if (!fpExtract(vm, "v", &v)) return false;
    if (GET_TYPE(v) == TYPE_CONTEXT) {
        Context* parent = fpToContext(v)->parent;
        if (parent) fpPush(vm, fpFromContext(parent));
        else fpPush(vm, VAL_NIL);
    } else {
        fpPush(vm, fpFromContext(vm->typeProtos[GET_TYPE(v)]));
    }
    return true;
}

bool builtin_sym(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    fpPush(vm, fpFromSymbol(fpIntern(str)));
    return true;
}

bool builtin_bitand(VM* vm) {
    int a, b;
    if (!fpExtract(vm, "ii", &a, &b)) return false;
    fpPush(vm, fpFromDouble(a & b));
    return true;
}

bool builtin_bitor(VM* vm) {
    int a, b;
    if (!fpExtract(vm, "ii", &a, &b)) return false;
    fpPush(vm, fpFromDouble(a | b));
    return true;
}

bool builtin_bitxor(VM* vm) {
    int a, b;
    if (!fpExtract(vm, "ii", &a, &b)) return false;
    fpPush(vm, fpFromDouble(a ^ b));
    return true;
}

bool builtin_bitnot(VM* vm) {
    int a;
    if (!fpExtract(vm, "i", &a)) return false;
    fpPush(vm, fpFromDouble(~a));
    return true;
}

bool builtin_strcat(VM* vm) {
    int n;
    const char** strs;
    if (!fpExtract(vm, "*s", &n, &strs)) return false;
    int length = 0;
    for (int i = 0; i < n; i++) length += strlen(strs[i]);
    char* buf = fpAlloc(length + 1);
    for (int i = 0; i < n; i++) {
        strcat(buf, strs[i]);
    }
    buf[length] = 0;
    // todo: some helper stack wipe function?
    memset(vm->stack->values, 0, vm->stack->capacity * sizeof(Value));
    fpPush(vm, fpFromString(buf));
    return true;
}

bool builtin_strmk(VM* vm) {
    int n;
    int* chars;
    if (!fpExtract(vm, "*i", &n, &chars)) return false;

    char* buf = fpAlloc(n + 1);
    for (int i = 0; i < n; i++) {
        buf[i] = chars[i];
    }
    buf[n] = 0;
    fpPush(vm, fpFromString(buf));
    return true;
}

bool builtin_strunmk(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    // todo: unicode
    for (int i = 0; str[i]; i++) {
        fpPush(vm, fpFromDouble(str[i]));
    }
    return true;
}

bool builtin_strtof(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    char* end;
    errno = 0;
    double d = strtod(str, &end);
    if (*end || errno) {
        fpRaiseInvalid(vm, "invalid number");
        return false;
    }
    fpPush(vm, fpFromDouble(d));
    return true;
}

bool builtin_strtoi(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    char* end;
    errno = 0;
    long l = strtol(str, &end, 10);
    if (*end || errno) {
        fpRaiseInvalid(vm, "invalid number");
        return false;
    }
    fpPush(vm, fpFromDouble(l));
    return true;
}

bool builtin_strlen(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    fpPush(vm, fpFromDouble(strlen(str)));
    return true;
}

bool builtin_strchr(VM* vm) {
    const char* haystack;
    int code;
    if (!fpExtract(vm, "si", &haystack, &code)) return false;
    const char* result = strchr(haystack, code);
    if (result) {
        fpPush(vm, fpFromDouble(result - haystack));
    } else {
        fpPush(vm, VAL_NIL);
    }
    return true;
}

bool builtin_strrchr(VM* vm) {
    const char* haystack;
    int code;
    if (!fpExtract(vm, "si", &haystack, &code)) return false;
    const char* result = strrchr(haystack, code);
    if (result) {
        fpPush(vm, fpFromDouble(result - haystack));
    } else {
        fpPush(vm, VAL_NIL);
    }
    return true;
}

bool builtin_strstr(VM* vm) {
    const char* haystack;
    const char* needle;
    if (!fpExtract(vm, "ss", &haystack, &needle)) return false;
    const char* result = strstr(haystack, needle);
    if (result) {
        fpPush(vm, fpFromDouble(result - haystack));
    } else {
        fpPush(vm, VAL_NIL);
    }
    return true;
}

bool builtin_stropen(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;

    char tmp[2];
    tmp[1] = 0;
    for (int i = 0; str[i]; i++) {
        // todo: stropen with cached strings for ascii values
        tmp[0] = str[i];
        fpPush(vm, fpFromString(GC_strdup(tmp)));
    }
    return true;
}

bool builtin_strsplit(VM* vm) {
    const char* str;
    const char* delim;
    if (!fpExtract(vm, "ss", &str, &delim)) return false;
    if (delim[0] == 0) {
        fpPush(vm, fpFromString(str));
        return builtin_stropen(vm);
    }
    int delimLen = strlen(delim);
    const char* begin = str;
    const char* end = strstr(str, delim);
    while (end) {
        const char* segment = GC_strndup(begin, end - begin);
        fpPush(vm, fpFromString(segment));
        begin = end + delimLen;
        end = strstr(begin, delim);
    }
    fpPush(vm, fpFromString(GC_strdup(begin)));
    return true;
}

bool builtin_strsub(VM* vm) {
    const char* str;
    bool hasBegin, hasEnd;
    int begin, end;
    if (!fpExtract(vm, "s?i?i",
        &str, &hasBegin, &begin, &hasEnd, &end)) return false;
    int len = strlen(str);
    if (!hasBegin) begin = 0;
    if (begin < 0) begin += len;
    if (!hasEnd) end = len;
    if (end < 0) end += len;
    if (begin < 0 || end < 0 || end < begin || begin > len || end > len) {
        // todo: separate #bounds exception type?
        fpRaiseInvalid(vm, "out of bounds");
        return false;
    }
    const char* result = GC_strndup(&str[begin], end - begin);
    fpPush(vm, fpFromString(result));
    return true;
}

bool builtin_strupper(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    char* result = GC_strdup(str);
    for (int i = 0; str[i]; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') {
            result[i] = c - 'a' + 'A';
        } else {
            result[i] = c;
        }
    }
    fpPush(vm, fpFromString(result));
    return true;
}

bool builtin_strlower(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    char* result = GC_strdup(str);
    for (int i = 0; str[i]; i++) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') {
            result[i] = c - 'A' + 'a';
        } else {
            result[i] = c;
        }
    }
    fpPush(vm, fpFromString(result));
    return true;
}

bool builtin_strisup(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    bool result = true;
    for (int i = 0; str[i]; i++) {
        char c = str[i];
        if (!(c >= 'A' && c <= 'Z')) {
            result = false;
            break;
        }
    }
    fpPush(vm, fpFromBool(result));
    return true;
}

bool builtin_strislo(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    bool result = true;
    for (int i = 0; str[i]; i++) {
        char c = str[i];
        if (!(c >= 'a' && c <= 'z')) {
            result = false;
            break;
        }
    }
    fpPush(vm, fpFromBool(result));
    return true;
}

// todo: this should probably be elsewhere (util?)
bool builtin_stresc(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    fpPush(vm, fpFromString(fpStringEscape(str)));
    return true;
}

bool builtin_strunesc(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    fpPush(vm, fpFromString(fpStringUnescape(str, INT32_MAX)));
    return true;
}

bool builtin_print(VM* vm) {
    const char* s;
    if (!fpExtract(vm, "s", &s)) return false;
    printf("%s", s);
    return true;
}

bool builtin_prompt(VM* vm) {
    const char* s;
    if (!fpExtract(vm, "s", &s)) return false;
    char* line = readline(s);
    if (!line) {
        fpPush(vm, fpNil);
    } else {
        fpPush(vm, fpFromString(GC_strdup(line)));
        free(line);
    }
    return true;
}

bool builtin_addhist(VM* vm) {
    const char* s;
    if (!fpExtract(vm, "s", &s)) return false;
    add_history(s);
    return true;
}

bool builtin_clock(VM* vm) {
    clock_t c = clock();
    fpPush(vm, fpFromDouble((double) c / CLOCKS_PER_SEC));
    return true;
}

bool builtin_gccollect(VM* vm) {
    GC_gcollect_and_unmap();
    return true;
}

bool builtin_gcdump(VM* vm) {
    GC_dump();
    printf("\n%lu used %lu free %lu unmapped\n",
        GC_get_heap_size(),
        GC_get_free_bytes(),
        GC_get_unmapped_bytes()
    );
    return true;
}

bool builtin_throw(VM* vm) {
    Symbol sym;
    const char* msg;
    if (!fpExtract(vm, "ys", &sym, &msg)) return false;
    // todo: somehow suppress builtin.so from showing up on exception
    vm->exSymbol = sym;
    vm->exMessage = msg;
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = false;
    return false;
}

bool builtin_exit(VM* vm) {
    int code;
    if (!fpExtract(vm, "i", &code)) return false;
    exit(code);
    return true;
}

bool builtin_list(VM* vm) {
    // todo: can we cheekily use less than full Stack size? (specifically
    // drop allocating previous)
    Stack* list = GC_MALLOC(sizeof(Stack));
    *list = (Stack) {
        .values = vm->stack->values,
        .next = vm->stack->next,
        .capacity = vm->stack->capacity
    };
    vm->stack->values = NULL;
    vm->stack->next = 0;
    vm->stack->capacity = 0;
    fpPush(vm, FROM_LIST(list));
    return true;
}

bool builtin_lstopen(VM* vm) {
    Stack* list;
    if (!fpExtract(vm, "l", &list)) return false;
    Stack_reserve(vm->stack, list->next);
    Value* base = &vm->stack->values[vm->stack->next];
    memcpy(base, list->values, list->next * sizeof(Value));
    vm->stack->next += list->next;
    return true;
}

bool builtin_lstpush(VM* vm) {
    Stack* list;
    Value v;
    if (!fpExtract(vm, "lv", &list, &v)) return false;
    Stack_push(list, v);
    return true;
}

bool builtin_lstpop(VM* vm) {
    Stack* list;
    if (!fpExtract(vm, "l", &list)) return false;
    if (list->next == 0) {
        // todo: separate #bounds exception type?
        fpRaiseInvalid(vm, "out of bounds");
        return false;
    }
    Value v = Stack_pop(list);
    Stack_push(vm->stack, v);
    return true;
}

bool builtin_lstget(VM* vm) {
    Stack* list;
    int index;
    if (!fpExtract(vm, "li", &list, &index)) return false;
    if (index < 0) index += list->next;
    if (index < 0 || index >= list->next) {
        // todo: separate #bounds exception type?
        fpRaiseInvalid(vm, "out of bounds");
        return false;
    }
    Stack_push(vm->stack, list->values[index]);
    return true;
}

bool builtin_lstset(VM* vm) {
    Stack* list;
    int index;
    Value v;
    if (!fpExtract(vm, "liv", &list, &index, &v)) return false;
    if (index < 0) index += list->next;
    if (index < 0 || index >= list->next) {
        // todo: separate #bounds exception type?
        fpRaiseInvalid(vm, "out of bounds");
        return false;
    }
    list->values[index] = v;
    return true;
}

bool builtin_lstsize(VM* vm) {
    Stack* list;
    if (!fpExtract(vm, "l", &list)) return false;
    fpPush(vm, fpFromDouble(list->next));
    return true;
}

bool builtin_lstchange(VM* vm) {
    Stack* list;
    if (!fpExtract(vm, "l", &list)) return false;
    GC_FREE(list->values);
    *list = (Stack) {
        .values = vm->stack->values,
        .next = vm->stack->next,
        .capacity = vm->stack->capacity
    };
    vm->stack->values = NULL;
    vm->stack->next = 0;
    vm->stack->capacity = 0;
    return true;
}

bool builtin_lstsub(VM* vm) {
    Stack* list;
    bool hasEnd;
    int begin, end;
    if (!fpExtract(vm, "li?i", &list, &begin, &hasEnd, &end)) return false;
    if (!hasEnd) end = list->next;
    if (begin < 0) begin += list->next;
    if (end < 0) end += list->next;
    if (begin < 0 || end < 0 || begin > list->next || end > list->next) {
        // todo: separate #bounds exception type?
        fpRaiseInvalid(vm, "out of bounds");
        return false;
    }
    int count = end - begin;
    Stack_reserve(vm->stack, count);
    Value* base = &vm->stack->values[vm->stack->next];
    memcpy(base, &list->values[begin], count * sizeof(Value));
    vm->stack->next += count;
    return true;
}

bool builtin_rand(VM* vm) {
    fpPush(vm, fpFromDouble(rand()));
    return true;
}

// todo: remove global variables by avoiding qsort
static VM* sort_vm;
static int compare_values(const void* p1, const void* p2) {
    if (!sort_vm) return 0;
    int a = *(int*) p1, b = *(int*) p2;
    int result = 0;
    Value* stk = sort_vm->stack->values;
    if (!fpValueCompare(sort_vm, stk[a], stk[b], &result)) {
        sort_vm = NULL;
        return 0;
    }
    return result;
}

bool builtin_sort(VM* vm) {
    int size = vm->stack->next;
    int* indices = GC_MALLOC_ATOMIC(sizeof(int) * size);
    for (int i = 0; i < size; i++) indices[i] = i;
    sort_vm = vm;
    qsort(indices, size, sizeof(int), compare_values);
    if (!sort_vm) return false;
    Value* newValues = GC_MALLOC_IGNORE_OFF_PAGE(vm->stack->capacity * sizeof(Value));
    for (int i = 0; i < size; i++) {
        newValues[i] = vm->stack->values[indices[i]];
    }
    GC_FREE(indices);
    GC_FREE(vm->stack->values);
    vm->stack->values = newValues;
    return true;
}

bool builtin_math1(VM* vm) {
    double x;
    int op;
    if (!fpExtract(vm, "di", &x, &op)) return false;
    double result;
    switch (op) {
        case 0: result = sin(x * (M_PI / 180)); break;
        case 1: result = cos(x * (M_PI / 180)); break;
        case 2: result = tan(x * (M_PI / 180)); break;
        case 3: result = asin(x) * (180 / M_PI); break;
        case 4: result = acos(x) * (180 / M_PI); break;
        case 5: result = atan(x) * (180 / M_PI); break;
        case 6: result = exp(x); break;
        case 7: result = exp2(x); break;
        case 8: result = log(x); break;
        case 9: result = log10(x); break;
        case 10: result = log2(x); break;
        case 11: result = sqrt(x); break;
        case 12: result = 1 / sqrt(x); break;
        case 13: result = floor(x); break;
        case 14: result = round(x); break;
        case 15: result = ceil(x); break;
        case 16: result = trunc(x); break;
        case 17: result = x - floor(x); break;
        case 18: result = copysign(1, x); break;
        case 19:
            switch (fpclassify(x)) {
                case FP_NAN: result = 0; break;
                case FP_INFINITE: result = 1; break;
                case FP_ZERO: result = 2; break;
                case FP_SUBNORMAL: result = 3; break;
                case FP_NORMAL: result = 4; break;
            } break;
        default:
            fpRaiseInvalid(vm, "invalid operation");
            return false;
    }
    fpPush(vm, FROM_NUMBER(result));
    return true;
}

bool builtin_math2(VM* vm) {
    double x, y;
    int op;
    if (!fpExtract(vm, "ddi", &x, &y, &op)) return false;
    double result;
    switch (op) {
        case 0: result = atan2(x, y) * (180 / M_PI); break;
        case 1: result = logb(x) / logb(y); break;
        default:
            fpRaiseInvalid(vm, "invalid operation");
            return false;
    }
    fpPush(vm, FROM_NUMBER(result));
    return true;
}

bool builtin_parse(VM* vm) {
    const char* source;
    Context* ctx;
    if (!fpExtract(vm, "sc", &source, &ctx)) return false;
    ModuleInfo* module = GC_MALLOC(sizeof(ModuleInfo));
    *module = (ModuleInfo) { "<parse>", source };
    Block* b = fpParse(module);
    if (!b) {
        fpRaiseInvalid(vm, "parse error");
        return false;
    }

    Closure* clos = GC_MALLOC(sizeof(Closure));
    clos->node = b->first;
    clos->binding = ctx;
    fpPush(vm, FROM_CLOSURE(clos));
    return true;
}

bool builtin_evalin(VM* vm) {
    Closure* clos;
    Context* ctx;
    if (!fpExtract(vm, "fc", &clos, &ctx)) return false;
    Context* oldCtx = vm->context;
    vm->context = ctx;
    // todo: this wont work for native fns... or a lot of things
    //       should try to use evalCall from vm.c
    Block b = { .first = clos->node };
    bool result = VM_eval(vm, &b);
    vm->context = oldCtx;
    if (!result) return false;
    return true;
}

bool builtin_sysctl(VM* vm) {
    int cmd;
    if (!fpExtract(vm, "i", &cmd)) return false;
    switch (cmd) {
        case 0: { // get version
            fpPush(vm, fpFromDouble(3.0));
        } break;
        default: {
            fpRaiseInvalid(vm, "invalid command number");
            return false;
        }
    }
    return true;
}

bool builtin_strblob(VM* vm) {
    const char* str;
    if (!fpExtract(vm, "s", &str)) return false;
    int len = strlen(str);
    fpPush(vm, Value_makeBlob(len, (const u8*) str));
    return true;
}

bool builtin_blobcat(VM* vm) {
    Blob* b1, *b2;
    if (!fpExtract(vm, "BB", &b1, &b2)) return false;
    int newLen = b1->size + b2->size;
    u8* newData = GC_MALLOC(newLen);
    memcpy(newData, b1->data, b1->size);
    memcpy(newData + b1->size, b2->data, b2->size);
    fpPush(vm, Value_makeBlob(newLen, newData));
    return true;
}

bool builtin_blobopen(VM* vm) {
    Blob* b;
    if (!fpExtract(vm, "B", &b)) return false;
    for (int i = 0; i < b->size; i++) {
        fpPush(vm, FROM_NUMBER(b->data[i]));
    }
    return true;
}

bool builtin_blobmk(VM* vm) {
    int len;
    int* bytes;
    if (!fpExtract(vm, "*i", &len, &bytes)) return false;
    u8* data = GC_MALLOC(len);
    for (int i = 0; i < len; i++) {
        data[i] = bytes[i];
    }
    fpPush(vm, Value_makeBlob(len, data));
    return true;
}

bool builtin_blobsub(VM* vm) {
    Blob* blob;
    bool hasBegin, hasEnd;
    int begin, end;
    if (!fpExtract(vm, "B?i?i",
        &blob, &hasBegin, &begin, &hasEnd, &end)) return false;
    int len = blob->size;
    if (!hasBegin) begin = 0;
    if (begin < 0) begin += len;
    if (!hasEnd) end = len;
    if (end < 0) end += len;
    if (begin < 0 || end < 0 || end < begin || begin > len || end > len) {
        // todo: separate #bounds exception type?
        fpRaiseInvalid(vm, "out of bounds");
        return false;
    }
    int newLen = end - begin;
    u8* newData = GC_MALLOC(newLen);
    memcpy(newData, blob->data + begin, newLen);
    fpPush(vm, Value_makeBlob(newLen, newData));
    return true;
}

bool builtin_bloblen(VM* vm) {
    Blob* blob;
    if (!fpExtract(vm, "B", &blob)) return false;
    fpPush(vm, fpFromDouble(blob->size));
    return true;
}

// todo: maybe move blob encoding/decoding logic to util?

static bool blobFmtCalc(VM* vm, const char* fmt, int* p_offs, int* p_size) {
    int offs = 0;
    int size = 0;
    int mod = 0;
    for (int i = 0; fmt[i]; i++) {
        char c = fmt[i];
        if (c >= '0' && c <= '9') {
            mod = mod * 10 + (c - '0');
        } else switch (c) {
            case 's': case 'S':
                if (mod == 0) {
                    fpRaiseInvalid(vm, "invalid encode format (string needs size)");
                    return false;
                }
                offs++; size += mod; mod = 0; break;
            case 'x':
                if (mod == 0) mod = 1;
                size += mod; mod = 0; break;
            case 'b': case 'B':
                if (mod == 0) mod = 1;
                offs += mod; size += mod; mod = 0; break;
            case 'h': case 'H':
                if (mod == 0) mod = 1;
                offs += mod; size += mod * 2; mod = 0; break;
            case 'i': case 'I': case 'f':
                if (mod == 0) mod = 1;
                offs += mod; size += mod * 4; mod = 0; break;
            case 'd':
                if (mod == 0) mod = 1;
                offs += mod; size += mod * 8; mod = 0; break;
            default:
                fpRaiseInvalid(vm, "invalid encode format (invalid char)");
                return false;
        }
    }
    if (mod != 0) {
        fpRaiseInvalid(vm, "invalid encode format (number at end)");
        return false;
    }
    *p_offs = offs; *p_size = size;
    return true;
}

bool builtin_blobenc(VM* vm) {
    const char* fmt;
    if (!fpExtract(vm, "s", &fmt)) return false;

    // calculate stack offset to start from and buf size
    int offs = 0;
    int size = 0;
    if (!blobFmtCalc(vm, fmt, &offs, &size)) return false;
    if (vm->stack->next < offs) {
        fpRaiseUnderflow(vm, offs);
        return false;
    }

    // process format chars
    int mod = 0;
    int pos = vm->stack->next - offs;
    u8* out = GC_MALLOC(size);
    memset(out, 0, size);
    int out_offs = 0;
    for (int i = 0; fmt[i]; i++) {
        char c = fmt[i];
        if (c >= '0' && c <= '9') {
            mod = mod * 10 + (c - '0');
        } else switch (c) {
            case 's': {
                Value v = vm->stack->values[pos++];
                if (GET_TYPE(v) != TYPE_STRING) {
                    fpRaiseType(vm, TYPE_STRING);
                    return false;
                }
                const char* str = GET_STRING(v);
                strncpy((char*) out + out_offs, str, mod);
                out_offs += mod; mod = 0;
            } break;
            case 'S': {
                Value v = vm->stack->values[pos++];
                if (GET_TYPE(v) != TYPE_BLOB) {
                    fpRaiseType(vm, TYPE_BLOB);
                    return false;
                }
                int len = GET_BLOB_SIZE(v);
                if (mod < len) len = mod;
                memcpy(out + out_offs, GET_BLOB_DATA(v), len);
                out_offs += mod; mod = 0;
            } break;
            case 'x': {
                if (mod == 0) mod = 1;
                out_offs += mod; mod = 0;
            } break;
            case 'b': case 'B': {
                if (mod == 0) mod = 1;
                for (int i = 0; i < mod; i++) {
                    Value v = vm->stack->values[pos++];
                    if (GET_TYPE(v) != TYPE_NUMBER) {
                        fpRaiseType(vm, TYPE_NUMBER);
                        return false;
                    }
                    out[out_offs + i] = (int) GET_NUMBER(v);
                }
                out_offs += mod; mod = 0;
            } break;
            case 'h': case 'H': {
                if (mod == 0) mod = 1;
                for (int i = 0; i < mod; i++) {
                    Value v = vm->stack->values[pos++];
                    if (GET_TYPE(v) != TYPE_NUMBER) {
                        fpRaiseType(vm, TYPE_NUMBER);
                        return false;
                    }
                    if (c == 'h') {
                        ((int16_t*)&out[out_offs])[i] = (int16_t) GET_NUMBER(v);
                    } else {
                        ((u16*)&out[out_offs])[i] = (u16) GET_NUMBER(v);
                    }
                }
                out_offs += mod * 2; mod = 0;
            } break;
            case 'i': case 'I': case 'f': {
                if (mod == 0) mod = 1;
                for (int i = 0; i < mod; i++) {
                    Value v = vm->stack->values[pos++];
                    if (GET_TYPE(v) != TYPE_NUMBER) {
                        fpRaiseType(vm, TYPE_NUMBER);
                        return false;
                    }
                    if (c == 'i') {
                        ((int32_t*)&out[out_offs])[i] = (int32_t) GET_NUMBER(v);
                    } else if (c == 'I') {
                        ((u32*)&out[out_offs])[i] = (u32) GET_NUMBER(v);
                    } else {
                        ((float*)&out[out_offs])[i] = (float) GET_NUMBER(v);
                    }
                }
                out_offs += mod * 4; mod = 0;
            } break;
            case 'd': {
                if (mod == 0) mod = 1;
                for (int i = 0; i < mod; i++) {
                    Value v = vm->stack->values[pos++];
                    if (GET_TYPE(v) != TYPE_NUMBER) {
                        fpRaiseType(vm, TYPE_NUMBER);
                        return false;
                    }
                    ((double*)&out[out_offs])[i] = (double) GET_NUMBER(v);
                }
                out_offs += mod * 8; mod = 0;
            } break;
        }
    }

    vm->stack->next -= offs;
    fpPush(vm, Value_makeBlob(size, out));

    return true;
}

bool builtin_blobdec(VM* vm) {
    Blob* blob;
    const char* fmt;
    if (!fpExtract(vm, "Bs", &blob, &fmt)) return false;

    int len = blob->size;
    const u8* data = blob->data;
    int offs = 0;
    int size = 0;
    blobFmtCalc(vm, fmt, &offs, &size);
    if (size != len) {
        fpRaiseInvalid(vm, "blob size does not match format");
        return false;
    }

    int mod = 0;
    int head = 0;
    for (int i = 0; fmt[i]; i++) {
        char c = fmt[i];
        if (c >= '0' && c <= '9') {
            mod = mod * 10 + (c - '0');
            continue;
        } else {
            if (mod == 0) mod = 1;
            switch (c) {
                case 'x': {
                    head += mod;
                } break;
                case 's': {
                    const char* str = GC_strndup((const char*) data+head, mod);
                    fpPush(vm, fpFromString(str));
                    head += mod;
                } break;
                case 'S': {
                    u8* data2 = GC_MALLOC(mod);
                    memcpy(data2, data + head, mod);
                    fpPush(vm, Value_makeBlob(mod, data2));
                    head += mod;
                } break;
                case 'b': {
                    for (int i = 0; i < mod; i++) {
                        int v = ((u8*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod;
                } break;
                case 'B': {
                    for (int i = 0; i < mod; i++) {
                        int v = ((int8_t*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod;
                } break;
                case 'h': {
                    for (int i = 0; i < mod; i++) {
                        int v = ((u16*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod * 2;
                } break;
                case 'H': {
                    for (int i = 0; i < mod; i++) {
                        int v = ((int16_t*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod * 2;
                } break;
                case 'i': {
                    for (int i = 0; i < mod; i++) {
                        u32 v = ((u32*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod * 4;
                } break;
                case 'I': {
                    for (int i = 0; i < mod; i++) {
                        int v = ((int32_t*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod * 4;
                } break;
                case 'f': {
                    for (int i = 0; i < mod; i++) {
                        float v = ((float*)&data[head])[i];
                        fpPush(vm, fpFromDouble(v));
                    } head += mod * 4;
                } break;
                case 'd': {
                    for (int i = 0; i < mod; i++) {
                        // todo: if nan tagging is ever used,
                        // need to change this here to check for nans
                        double d = ((double*)&data[head])[i];
                        fpPush(vm, fpFromDouble(d));
                    } head += mod * 4;
                } break;
            }
            mod = 0;
        }
    }
    return true;
}

bool builtin_read(VM* vm) {
    const char* path;
    if (!fpExtract(vm, "s", &path)) return false;
    FILE* f = fopen(path, "r");
    if (!f) {
        // todo: better type than #invalid (#io?)
        fpRaiseInvalid(vm, "file not found");
        return false;
    }
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buffer = GC_MALLOC(size + 1);
    if (fread(buffer, size, 1, f) != 1) {
        fclose(f);
        // todo: better type than #invalid (#io?)
        fpRaiseInvalid(vm, "error reading file");
        return false;
    }
    buffer[size] = 0;
    fclose(f);
    fpPush(vm, fpFromString(buffer));
    return true;
}

bool builtin_write(VM* vm) {
    const char* path;
    const char* str;
    if (!fpExtract(vm, "ss", &path, &str)) return false;
    FILE* f = fopen(path, "w");
    if (!f) {
        // todo: better type than #invalid (#io?)
        fpRaiseInvalid(vm, "could not open file");
        return false;
    }
    if (fwrite(str, strlen(str), 1, f) != 1) {
        fclose(f);
        // todo: better type than #invalid (#io?)
        fpRaiseInvalid(vm, "error writing file");
        return false;
    }
    fclose(f);
    return true;
}

#include <dirent.h>
bool builtin_dir(VM* vm) {
    const char* path;
    if (!fpExtract(vm, "s", &path)) return false;
    DIR* d = opendir(path);
    if (!d) {
        // todo: better type than #invalid (#io?)
        fpRaiseInvalid(vm, "could not open directory");
        return false;
    }
    while (1) {
        struct dirent* de = readdir(d);
        if (!de) break;
        fpPush(vm, fpFromString(GC_strdup(de->d_name)));
    }
    closedir(d);
    return true;
}

bool builtin_test(VM* vm) {
    int n;
    int* ns;
    const char* s1, * s2;
    if (!fpExtract(vm, "s*is", &s1, &n, &ns, &s2)) return false;
    for (int i = 0; i < n; i++) printf("[%d] = %d\n", i, ns[i]);
    printf("%s.%s\n", s1, s2);
    return true;
}

#define REGISTER(name) \
    fpContextBind(ctx, fpIntern(#name), \
        fpFromFunction(module, "builtin_" #name, builtin_##name))

bool register_module(VM* vm, ModuleInfo* module) {
    Context* ctx = Context_create(NULL);
    module->value = FROM_CONTEXT(ctx);
    REGISTER(stksize);
    REGISTER(reverse);
    REGISTER(dump);
    REGISTER(ctxdump);
    REGISTER(valstr);
    REGISTER(getv);
    REGISTER(setv);
    REGISTER(bindv);
    REGISTER(hasv);
    REGISTER(lsv);
    REGISTER(type);
    REGISTER(getp);
    REGISTER(sym);
    REGISTER(bitand);
    REGISTER(bitor);
    REGISTER(bitxor);
    REGISTER(bitnot);
    REGISTER(print);
    REGISTER(prompt);
    REGISTER(addhist);
    REGISTER(strcat);
    REGISTER(strmk);
    REGISTER(strunmk);
    REGISTER(strtof);
    REGISTER(strtoi);
    REGISTER(strlen);
    REGISTER(strchr);
    REGISTER(strrchr);
    REGISTER(strstr);
    REGISTER(stropen);
    REGISTER(strsplit);
    REGISTER(strsub);
    REGISTER(strupper);
    REGISTER(strlower);
    REGISTER(strisup);
    REGISTER(strislo);
    REGISTER(stresc);
    REGISTER(strunesc);
    REGISTER(clock);
    REGISTER(gccollect);
    REGISTER(gcdump);
    REGISTER(throw);
    REGISTER(exit);
    REGISTER(list);
    REGISTER(lstopen);
    REGISTER(lstpush);
    REGISTER(lstpop);
    REGISTER(lstget);
    REGISTER(lstset);
    REGISTER(lstsize);
    REGISTER(lstchange);
    REGISTER(lstsub);
    REGISTER(rand);
    REGISTER(sort);
    REGISTER(math1);
    REGISTER(math2);
    REGISTER(parse);
    REGISTER(evalin);
    REGISTER(sysctl);
    REGISTER(strblob);
    REGISTER(blobcat);
    REGISTER(blobopen);
    REGISTER(blobmk);
    REGISTER(blobsub);
    REGISTER(bloblen);
    REGISTER(blobenc);
    REGISTER(blobdec);
    REGISTER(read);
    REGISTER(write);
    REGISTER(dir);
    REGISTER(test);
    return true;
}
