#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "../fruity.h"

// todo: cache recently used code objects to avoid recompiling each time?

typedef struct {
    pcre2_code* code;
    pcre2_match_data* match_data;
    bool prevWasEmpty; // indicates next match should be non empty
    int offset;
    int subjlen;
} RegexData;

typedef PCRE2_SIZE* OVector;

static bool re_prepare(VM* vm, RegexData* re, const char* pattern) {
    int errnum;
    PCRE2_SIZE erroffs;
    re->code = pcre2_compile(
        (PCRE2_SPTR) pattern,
        PCRE2_ZERO_TERMINATED, 0,
        &errnum, &erroffs, NULL);
    if (!re->code) {
        char msg[256];
        pcre2_get_error_message(errnum, (PCRE2_UCHAR*) msg, 256);
        fpRaiseInvalid(vm, fpSprintf("at offset %d: %s", (int) erroffs, msg));
        return false;
    }

    re->match_data = pcre2_match_data_create_from_pattern_8(re->code, NULL);
    return true;
}

static void re_cleanup(RegexData* re) {
    pcre2_match_data_free(re->match_data);
    pcre2_code_free(re->code);
}

static bool re_match(VM* vm, RegexData* re, const char* subject,
    int* count, OVector* out) {
    if (re->subjlen == 0) re->subjlen = strlen(subject);

    uint32_t options = 0;
    if (re->prevWasEmpty) {
        options |= PCRE2_NOTEMPTY_ATSTART;
    }
    
    int rc = pcre2_match(
        re->code, (PCRE2_SPTR) subject, re->subjlen,
        re->offset, options, re->match_data, NULL);
    
    if (rc < 0) {
        if (rc == PCRE2_ERROR_NOMATCH) {
            *count = 0;
            return true;
        } else {
            char msg[256];
            pcre2_get_error_message(rc, (PCRE2_UCHAR*) msg, 256);
            // todo: expose gc_strdup
            fpRaiseInvalid(vm, fpSprintf("%s", msg));
            return false;
        }
    }

    // shouldn't happen?
    if (rc == 0) {
        fpRaiseInternal(vm, "ovector wasnt big enough");
        return false;
    }

    *count = rc;
    *out = pcre2_get_ovector_pointer(re->match_data);

    re->offset = (*out)[1];
    if ((*out)[0] == (*out)[1]) {
        re->prevWasEmpty = true;
    } else {
        PCRE2_SIZE start = pcre2_get_startchar(re->match_data);
        if (re->offset <= start && start < re->subjlen) {
            // todo: utf8 support
            re->offset++;
        }
        re->prevWasEmpty = false;
    }

    return true;
}

bool modpcre2_match(VM* vm) {
    const char* pattern;
    const char* subject;
    if (!fpExtract(vm, "ss", &pattern, &subject)) return false;
    RegexData re = {};
    if (!re_prepare(vm, &re, pattern)) return false;
    
    int count;
    OVector ovector;
    if (!re_match(vm, &re, subject, &count, &ovector)) goto failure;

    for (int i = 0; i < count * 2; i++) {
        fpPush(vm, fpFromDouble(ovector[i]));
    }
    
    re_cleanup(&re);
    return true;

    failure:
    re_cleanup(&re);
    return false;
}

bool modpcre2_match_all(VM* vm) {
    const char* pattern;
    const char* subject;
    if (!fpExtract(vm, "ss", &pattern, &subject)) return false;
    RegexData re = {};
    if (!re_prepare(vm, &re, pattern)) return false;
    
    while (1) {
        int count;
        OVector ovector;
        if (!re_match(vm, &re, subject, &count, &ovector)) goto failure;

        if (count == 0) break;

        fpBeginList(vm);
        for (int i = 0; i < count * 2; i++) {
            fpPush(vm, fpFromDouble(ovector[i]));
        }
        fpEndList(vm);
    }
    
    re_cleanup(&re);
    return true;

    failure:
    re_cleanup(&re);
    return false;
}

#define REGISTER(name) \
    fpContextBind(ctx, fpIntern(#name), \
        fpFromFunction(module, "modpcre2_" #name, modpcre2_##name))

bool register_module(VM* vm, ModuleInfo* module) {
    Context* ctx = fpContextCreate(NULL);
    fpModuleSetValue(module, fpFromContext(ctx));
    REGISTER(match);
    REGISTER(match_all);
    return true;
}
