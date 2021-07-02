#include <stdlib.h>

#include "parson.h"
#include "../value.h"
#include "../symbols.h"
#include "../context.h"
#include "../stack.h"
#include "../fruity.h"

static Value json_to_fruity(VM* vm, JSON_Value* j) {
    switch (json_value_get_type(j)) {
        case JSONNull: return fpNil;
        case JSONString: return fpFromString(GC_strdup(json_value_get_string(j)));
        case JSONNumber: return fpFromDouble(json_value_get_number(j));
        case JSONObject: {
            Context* c = fpContextCreate(NULL);
            JSON_Object* o = json_value_get_object(j);
            int nkeys = json_object_get_count(o);
            for (int i = 0; i < nkeys; i++) {
                Symbol key = fpIntern(json_object_get_name(o, i));
                Value v = json_to_fruity(vm, json_object_get_value_at(o, i));
                fpContextBind(c, key, v);
            }
            return fpFromContext(c);
        }
        case JSONArray: {
            JSON_Array* a = json_value_get_array(j);
            int nvals = json_array_get_count(a);
            // todo: api for lists that doesnt require vm?
            fpBeginList(vm);
            for (int i = 0; i < nvals; i++) {
                fpPush(vm, json_to_fruity(vm, json_array_get_value(a, i)));
            }
            fpEndList(vm);
            return fpPop(vm);
        }
        case JSONBoolean: return fpFromBool(json_value_get_boolean(j));
    }
    return fpNil;
}

// todo: currently fruity_to_json needs fields that aren't exposed in fruity.h
static JSON_Value* fruity_to_json(Value value) {
    switch (GET_TYPE(value)) {
        case TYPE_NUMBER: return json_value_init_number(fpToDouble(value));
        case TYPE_SYMBOL:
            return json_value_init_string(Symbol_name(fpToSymbol(value)));
        case TYPE_STRING: return json_value_init_string(fpToString(value));
        case TYPE_ODDBALL: {
            switch (GET_ODDBALL(value)) {
                case 0: return json_value_init_boolean(true);
                case 1: return json_value_init_boolean(false);
                default: return json_value_init_null();
            }
        }
        case TYPE_CONTEXT: {
            Context* ctx = fpToContext(value);
            JSON_Value* result = json_value_init_object();
            JSON_Object* o = json_value_get_object(result);
            for (int i = 0; i < ctx->capacity; i++) {
                if (!ctx->keys[i]) continue;
                const char* name = Symbol_name(ctx->keys[i]);
                JSON_Value* jv = fruity_to_json(ctx->values[i]);
                json_object_set_value(o, name, jv);
            }
            return result;
        }
        case TYPE_CLOSURE: return json_value_init_null();
        case TYPE_LIST: {
            Stack* s = GET_LIST(value);
            JSON_Value* result = json_value_init_array();
            JSON_Array* a = json_value_get_array(result);
            for (int i = 0; i < s->next; i++) {
                JSON_Value* jv = fruity_to_json(s->values[i]);
                json_array_append_value(a, jv);
            }
            return result;
        }
        case TYPE_BLOB: {
            int size = GET_BLOB_SIZE(value);
            const u8* data = GET_BLOB_DATA(value);
            JSON_Value* result = json_value_init_array();
            JSON_Array* a = json_value_get_array(result);
            for (int i = 0; i < size; i++) {
                JSON_Value* jv = json_value_init_number(data[i]);
                json_array_append_value(a, jv);
            }
            return result;
        }
    }
    return json_value_init_null();
}

bool modjson_decode(VM* vm) {
    const char* json_source;
    if (!fpExtract(vm, "s", &json_source)) return false;
    JSON_Value* v = json_parse_string_with_comments(json_source);
    if (!v) {
        fpRaiseInvalid(vm, "error parsing json string");
        return false;
    }
    fpPush(vm, json_to_fruity(vm, v));
    json_value_free(v);
    return true;
}

bool modjson_encode(VM* vm) {
    Value value;
    if (!fpExtract(vm, "v", &value)) return false;
    JSON_Value* j = fruity_to_json(value);
    size_t size = json_serialization_size(j);
    char* buf = fpAllocData(size);
    JSON_Status s = json_serialize_to_buffer(j, buf, size);
    json_value_free(j);
    if (s != JSONSuccess) {
        fpRaiseInvalid(vm, "error encoding json string");
        return false;
    }
    fpPush(vm, fpFromString(buf));
    return true;
}

bool modjson_encode_pretty(VM* vm) {
    Value value;
    if (!fpExtract(vm, "v", &value)) return false;
    JSON_Value* j = fruity_to_json(value);
    size_t size = json_serialization_size_pretty(j);
    char* buf = fpAllocData(size);
    JSON_Status s = json_serialize_to_buffer_pretty(j, buf, size);
    json_value_free(j);
    if (s != JSONSuccess) {
        fpRaiseInvalid(vm, "error encoding json string");
        return false;
    }
    fpPush(vm, fpFromString(buf));
    return true;
}

#define REGISTER(name) \
    fpContextBind(ctx, fpIntern(#name), \
        fpFromFunction(module, "modjson_" #name, modjson_##name))

bool register_module(VM* vm, ModuleInfo* module) {
    Context* ctx = fpContextCreate(NULL);
    fpModuleSetValue(module, fpFromContext(ctx));
    REGISTER(decode);
    REGISTER(encode);
    REGISTER(encode_pretty);
    return true;
}
