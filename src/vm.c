#include "vm.h"
#include "context.h"
#include "parser.h"
#include "stack.h"
#include "value.h"
#include <math.h>

typedef enum {
    RESOLVE_FAIL, // could not resolve
    RESOLVE_NOSELF, // resolved, no self
    RESOLVE_SELF // resolved, with self
} ResolveStatus;

// todo: expose via header
extern const char* gc_sprintf(const char* fmt, ...);

// todo: this should be in vm
extern Stack* genTraceList(VM* vm);

static bool evalNode(VM* vm, AstNode* node);
static ResolveStatus chainResolve(VM* vm, AstNode* node,
    Context** base, AstChainElem** chainElem, Value* self);
static Context* getContext(VM* vm, Value v);
bool evalCall(VM* vm, AstNode* caller, Value v, Value* self);
static bool applyOperator(VM* vm, AstNode* node, Value lhs, int op);
static bool isTruthy(Value v);
static bool evalAndPop(VM* vm, AstNode* node, Value* value);
static bool evalSpecial(VM* vm, AstNode* node, int special, Value sub);

void raiseUnbound(VM* vm, AstNode* node, Symbol sym);
void raiseUnbound2(VM* vm, AstNode* node, Symbol sym, Value value);
void raiseUnderflow(VM* vm, AstNode* node, int n);
void raiseType(VM* vm, AstNode* node, Type type);
void raiseInvalid(VM* vm, AstNode* node, const char* msg);
void raiseInternal(VM* vm, const char* msg);
static void traceNode(VM* vm, AstNode* node);
static void traceNative(VM* vm, NativeClosure* nc);

// idea: have current stack inline in vm?

static const char* opNames[] = {
    "_add", "_sub", "_mul", "_div",
    "_eq", "_neq", "_lt", "_gt", "_lteq", "_gteq",
    "_pow", "_mod", "_ltlt", "_gtgt", "_and", "_or",
    "_addadd", "_subsub", "_mulmul", "_divmod", "_cmp"
};

static const char* exNames[] = {
    "unbound", "underflow", "type", "invalid", "internal"
};

void VM_startup(VM* vm) {
    vm->stack = GC_MALLOC(sizeof(Stack));
    *vm->stack = (Stack) {};
    vm->root = NULL;
    vm->context = NULL;
    // todo: have these pre-initialized to fixed indices?
    vm->symSelf = Symbol_find("self", 4);
    vm->symThis = Symbol_find("this", 4);
    for (int i = 0; i < 21; i++) {
        vm->symOps[i] = Symbol_find(opNames[i], strlen(opNames[i]));
    }
    for (int i = 0; i < 5; i++) {
        vm->symExs[i] = Symbol_find(exNames[i], strlen(exNames[i]));
    }
    vm->symKey = Symbol_find("key", 3);
    vm->symValue = Symbol_find("value", 5);
    vm->symMessage = Symbol_find("message", 7);
    vm->symTrace = Symbol_find("trace", 5);
    vm->modules = NULL;
    vm->moduleCount = 0;
    for (int i = 0; i < 8; i++) {
        if (i == TYPE_CONTEXT) continue;
        vm->typeProtos[i] = Context_create(NULL);
    }
    vm->refProto = Context_create(NULL);
    vm->argProto = Context_create(NULL);
    vm->exProto = Context_create(NULL);
}

bool VM_eval(VM* vm, Block* block) {
    if (block->first) {
        if (!evalNode(vm, block->first)) return false;
    }
    return true;
}

bool VM_evalModule(VM* vm, Block* block, Context* ctx) {
    Stack* oldStack = vm->stack;
    Context* oldCtx = vm->context;
    vm->stack = GC_MALLOC(sizeof(Stack));
    *vm->stack = (Stack) {};
    vm->context = ctx;
    if (block->first) {
        if (!evalNode(vm, block->first)) return false;
    }
    vm->context = oldCtx;
    vm->stack = oldStack;
    return true;
}

#define PUSH(x) Stack_push(vm->stack, (x))

static bool evalNode(VM* vm, AstNode* node) {
    switch (node->kind) {
        case AST_NUMBER: {
            PUSH(FROM_NUMBER(node->as_number));
        } break;
        case AST_SYMBOL: {
            PUSH(FROM_SYMBOL(node->as_symbol));
        } break;
        case AST_STRING: {
            PUSH(FROM_STRING(node->as_string));
        } break;
        case AST_ODDBALL: {
            PUSH(FROM_ODDBALL(node->as_int));
        } break;
        case AST_CLOSURE: {
            Closure* closure = GC_MALLOC(sizeof(Closure));
            *closure = (Closure) { node->sub, vm->context };
            PUSH(FROM_CLOSURE(closure));
        } break;
        case AST_OBJECT: {
            Context* oldCtx = vm->context;
            vm->context = Context_create(vm->context);
            bool result = true;
            if (node->sub) result = evalNode(vm, node->sub);
            vm->context->parent = NULL;
            PUSH(FROM_CONTEXT(vm->context));
            vm->context = oldCtx;
            if (!result) return false;
        } break;
        case AST_CALLV: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            Value self;
            ResolveStatus rs = chainResolve(vm, node, &base, &chainElem, &self);
            if (!rs) return false;
            Value* pv = Context_get(base, chainElem->symbol);
            if (!pv) {
                raiseUnbound(vm, node, chainElem->symbol);
                return false;
            }
            if (!evalCall(vm, node, *pv, rs == RESOLVE_SELF ? &self : NULL)) {
                return false;
            }
        } break;
        case AST_GETV: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            if (!chainResolve(vm, node, &base, &chainElem, NULL)) return false;
            Value* pv = Context_get(base, chainElem->symbol);
            if (!pv) {
                raiseUnbound(vm, node, chainElem->symbol);
                return false;
            }
            PUSH(*pv);
        } break;
        case AST_SETV: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            if (!chainResolve(vm, node, &base, &chainElem, NULL)) return false;
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value v = Stack_pop(vm->stack);
            if (!Context_set(base, chainElem->symbol, v)) {
                raiseUnbound(vm, node, chainElem->symbol);
                return false;
            }
        } break;
        case AST_BINDV: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            if (!chainResolve(vm, node, &base, &chainElem, NULL)) return false;
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value v = Stack_pop(vm->stack);
            Context_bind(base, chainElem->symbol, v);
        } break;
        case AST_HASV: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            if (!chainResolve(vm, node, &base, &chainElem, NULL)) return false;
            Value* pv = Context_get(base, chainElem->symbol);
            PUSH(pv ? VAL_TRUE : VAL_FALSE);
        } break;
        case AST_REFV: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            Value self;
            ResolveStatus rs = chainResolve(vm, node, &base, &chainElem, &self);
            if (!rs) return false;
            Context* ref = Context_create(vm->refProto);
            // todo: should we just use the self that was resolved? or go up
            // parent chain to find context where already bound if possible?
            Context_bind(ref, vm->symSelf, rs == RESOLVE_SELF ? self : FROM_CONTEXT(vm->context));
            Context_bind(ref, vm->symKey, FROM_SYMBOL(chainElem->symbol));
            Stack_push(vm->stack, FROM_CONTEXT(ref));
        } break;
        case AST_PREBIND: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            if (!chainResolve(vm, node, &base, &chainElem, NULL)) return false;
            // todo: evalAndPop?
            if (!evalNode(vm, node->sub)) {
                return false;
            }
            Value v = Stack_pop(vm->stack);
            Context_bind(base, chainElem->symbol, v);
        } break;
        case AST_PRECALL: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            Value self;
            ResolveStatus rs = chainResolve(vm, node, &base, &chainElem, &self);
            if (!rs) return false;
            Value* pv = Context_get(base, chainElem->symbol);
            if (!pv) {
                raiseUnbound(vm, node, chainElem->symbol);
                return false;
            }
            Stack* oldStk = vm->stack;
            vm->stack = GC_MALLOC(sizeof(Stack));
            *vm->stack = (Stack) { .previous = oldStk };
            if (node->sub && !evalNode(vm, node->sub)) {
                Stack_move(vm->stack, oldStk, vm->stack->next);
                vm->stack = oldStk;
                return false;
            }
            bool result = evalCall(vm, node, *pv, rs == RESOLVE_SELF ? &self : NULL);
            Stack_move(vm->stack, oldStk, vm->stack->next);
            GC_FREE(vm->stack->values);
            GC_FREE(vm->stack);
            vm->stack = oldStk;
            if (!result) return false;
        } break;
        case AST_PRECALL_BARE: {
            Context* base = vm->context;
            AstChainElem* chainElem = node->as_chain;
            Value self;
            ResolveStatus rs = chainResolve(vm, node, &base, &chainElem, &self);
            if (!rs) return false;
            Value* pv = Context_get(base, chainElem->symbol);
            if (!pv) {
                raiseUnbound(vm, node, chainElem->symbol);
                return false;
            }
            if (!evalNode(vm, node->sub)) {
                return false;
            }
            if (!evalCall(vm, node, *pv, rs == RESOLVE_SELF ? &self : NULL)) {
                return false;
            }
        } break;
        case AST_OPERATOR: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value lhs = Stack_pop(vm->stack);
            // todo: eval and pop rhs?
            //       may be faster for builtin ops?
            if (!evalNode(vm, node->sub)) {
                return false;
            }
            if (!applyOperator(vm, node, lhs, node->as_int)) {
                return false;
            }
        } break;
        case AST_ARGUMENT: {
            Value v;
            if (!evalAndPop(vm, node->sub, &v)) return false;
            Context* ref = Context_create(vm->argProto);
            Context_bind(ref, vm->symKey, FROM_SYMBOL(node->as_symbol));
            Context_bind(ref, vm->symValue, v);
            Stack_push(vm->stack, FROM_CONTEXT(ref));
        } break;
        case AST_GROUP: {
            if (!node->sub) break;
            Stack* old = vm->stack;
            vm->stack = GC_MALLOC(sizeof(Stack));
            *vm->stack = (Stack) { .previous = old };
            bool result = evalNode(vm, node->sub);
            if (result) Stack_move(vm->stack, old, vm->stack->next);
            GC_FREE(vm->stack->values);
            GC_FREE(vm->stack);
            vm->stack = old;
            if (!result) return false;
        } break;
        case AST_DOTS: {
            if (!vm->stack->previous) {
                raiseUnderflow(vm, node, -1);
                return false;
            } else if (Stack_size(vm->stack->previous) < node->as_int) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Stack_move(vm->stack->previous, vm->stack, node->as_int);
        } break;
        case AST_THEN_ELSE: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value cond = Stack_pop(vm->stack);
            Value caseTrue, caseFalse;
            if ((node->sub && !evalAndPop(vm, node->sub, &caseTrue)) ||
                (node->as_node && !evalAndPop(vm, node->as_node, &caseFalse))) {
                return false;
            }
            if (isTruthy(cond)) {
                // todo: should we pass node->sub/node->as_node for the caller node?
                // if we ignore closures going direct into then/until stmts then
                // this would look cleaner maybe?
                // alternatively, could then/until nodes have their range expanded
                // to include subs also? then would highlight whole region
                if (node->sub && !evalCall(vm, node, caseTrue, NULL)) {
                    return false;
                }
            } else {
                if (node->as_node && !evalCall(vm, node, caseFalse, NULL)) {
                    return false;
                }
            }
        } break;
        case AST_UNTIL_DO: {
            Value caseUntil, caseDo;
            if ((node->sub && !evalAndPop(vm, node->sub, &caseUntil)) ||
                (node->as_node && !evalAndPop(vm, node->as_node, &caseDo))) {
                return false;
            }
            while (1) {
                // eval until part (if true break)
                if (node->sub) {
                    if (node->sub && !evalCall(vm, node, caseUntil, NULL)) {
                        return false;
                    }
                    if (vm->stack->next == 0) {
                        raiseUnderflow(vm, node, 1);
                        return false;
                    }
                    Value cond = Stack_pop(vm->stack);
                    if (isTruthy(cond)) break;
                }
                // eval do part
                if (node->as_node && !evalCall(vm, node, caseDo, NULL)) {
                    return false;
                }
            }
        } break;
        case AST_SPECIAL: {
            Value sub;
            if (!evalAndPop(vm, node->sub, &sub)) return false;
            if (!evalSpecial(vm, node, node->as_int, sub)) return false;
        } break;
        case AST_IMPORT: {
            AstChainElem* chain = node->as_chain;
            if (chain->symbol == (Symbol) -1) assert(0 && "not impl");
            if (!Module_import(vm, Symbol_name(chain->symbol), false)) {
                return false;
            }
            // todo: Module_import should probably just return Value
            Value v = Stack_pop(vm->stack);
            while (chain->next) {
                Value* pv = Context_get(getContext(vm, v), chain->next->symbol);
                if (!pv) {
                    raiseUnbound(vm, node, chain->next->symbol);
                    return false;
                }
                v = *pv;
                chain = chain->next;
            }
            if (node->sub) {
                Context_bind(vm->context, node->sub->as_chain->symbol, v);
            } else {
                Context_bind(vm->context, chain->symbol, v);
            }
        } break;
        case AST_THIS: {
            PUSH(FROM_CONTEXT(vm->context));
        } break;
        case AST_SIGBIND: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value v = Stack_pop(vm->stack);
            Context_bind(vm->context, node->as_symbol, v);
        } break;
        default: {
            assert(0 && "not impl");
        }
    }
    if (node->next) return evalNode(vm, node->next);
    else return true;
}

static ResolveStatus chainResolve(VM* vm, AstNode* node,
    Context** base, AstChainElem** chainElem, Value* self) {
    Value val;
    bool hasSelf = (*chainElem)->next != NULL;
    while ((*chainElem)->next) {
        if ((*chainElem)->symbol == (Symbol) -1) {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return RESOLVE_FAIL;
            }
            val = Stack_pop(vm->stack);
            *base = getContext(vm, val);
        } else {
            Value* pv = Context_get(*base, (*chainElem)->symbol);
            if (!pv) {
                raiseUnbound(vm, node, (*chainElem)->symbol);
                return RESOLVE_FAIL;
            } else {
                val = *pv;
                *base = getContext(vm, *pv);
            }
        }
        *chainElem = (*chainElem)->next;
    }
    if (hasSelf && self) *self = val;
    return hasSelf ? RESOLVE_SELF : RESOLVE_NOSELF;
}

static Context* getContext(VM* vm, Value v) {
    // todo: make this a macro for perf?
    //       can do something like:
    //  GET_TYPE(v) == TYPE_CONTEXT ? GET_CONTEXT(v) : vm->protos[GET_TYPE(v)]
    // todo: maybe change name so different from GET_CONTEXT?
    Type t = GET_TYPE(v);
    if (t == TYPE_CONTEXT) {
        return GET_CONTEXT(v);
    } else {
        return vm->typeProtos[t];
    }
}

bool evalCall(VM* vm, AstNode* caller, Value v, Value* self) {
    if (GET_TYPE(v) == TYPE_CLOSURE) {
        Closure* closure = GET_CLOSURE(v);
        if (!closure->binding) { // is native
            NativeClosure* nc = (NativeClosure*) closure;
            // todo: check native function correctly raised exception on failure
            bool result = ((NativeFn) nc->nativeFn)(vm);
            if (!result) {
                if (!vm->exSourceHasTrace && !vm->exTraceFirst) {
                    vm->exSourceHasTrace = true;
                }
                traceNative(vm, nc);
                traceNode(vm, caller);
            }
            return result;
        }
        Context* oldCtx = vm->context;
        vm->context = Context_create(closure->binding);
        // todo: somehow block dotting through calls?
        //       temporarily set stack->previous to null?
        //       note that we don't always want to block on call
        //       (e.g. object syntax, loops)
        //       maybe don't block?
        // note: 1.0 does not block
        if (self) {
            Context_bind(vm->context, vm->symSelf, *self);
        }
        bool result = true;
        if (closure->node) result = evalNode(vm, closure->node);
        vm->context = oldCtx;
        if (!result) {
            traceNode(vm, caller);
            return false;
        }
    } else {
        // todo: lookup _apply?
        PUSH(v);
    }
    return true;
}

static int compareNums(double a, double b) {
    // todo: more detail comparison method?
    if (a < b) return -1;
    else if (a > b) return 1;
    else return 0;
}

bool valueCompare(VM* vm, AstNode* node, Value lhs, Value rhs, int* result) {
    Type t = GET_TYPE(lhs);
    // todo: if we always return false how does that impact sorting order?
    if (GET_TYPE(rhs) != t) *result = t - GET_TYPE(rhs);
    else switch (t) {
        case TYPE_NUMBER: {
            *result = compareNums(GET_NUMBER(lhs), GET_NUMBER(rhs));
        } break;
        case TYPE_SYMBOL: {
            *result = strcmp(Symbol_name(GET_SYMBOL(lhs)), Symbol_name(GET_SYMBOL(rhs)));
        } break;
        case TYPE_STRING: {
            *result = strcmp(GET_STRING(lhs), GET_STRING(rhs));
        } break;
        case TYPE_ODDBALL: {
            // todo: is this how we want to order oddballs?
            int a = GET_ODDBALL(lhs), b = GET_ODDBALL(rhs);
            if (a < b) *result = -1;
            else if (a > b) *result = 1;
            else *result = 0;
        } break;
        case TYPE_CONTEXT: {
            // todo: store #_cmp
            Symbol symCmp = Symbol_find("_cmp", 4);
            Value* pfn = Context_get(GET_CONTEXT(lhs), symCmp);
            if (!pfn) {
                raiseUnbound2(vm, node, symCmp, lhs);
                return false;
            } else {
                int oldStk = vm->stack->next;
                Stack_push(vm->stack, rhs);
                if (!evalCall(vm, node, *pfn, &lhs)) return false;
                if (vm->stack->next != oldStk + 1) {
                    raiseInvalid(vm, node, "_cmp metamethod returned incorrect amount of values");
                    return false;
                }
                Value cm = Stack_pop(vm->stack);
                if (GET_TYPE(cm) != TYPE_NUMBER) {
                    // todo: this should probably be #type
                    raiseInvalid(vm, node, "_cmp metamethod returned incorrect type");
                    return false;
                }
                *result = compareNums(GET_NUMBER(cm), 0);
            }
        } break;
        case TYPE_CLOSURE: {
            *result = 0;
        } break;
        case TYPE_LIST: {
            Stack* llist = GET_LIST(lhs), * rlist = GET_LIST(rhs);
            if (llist == rlist) {
                *result = 0; return true;
            }
            bool lhsSmaller = llist->next < rlist->next;
            int minSize = lhsSmaller ? llist->next : rlist->next;
            for (int i = 0; i < minSize; i++) {
                // todo: this will recurse infinitely if a list contains itself
                //       should there be some max recursion depth for comparison?
                if (!valueCompare(vm, node, llist->values[i], rlist->values[i], result)) return false;
                if (*result != 0) return true;
            }
            if (llist->next != rlist->next) {
                *result = lhsSmaller ? -1 : 1;
            } else {
                *result = 0;
            }
        } break;
        default: {
            assert(0 && "not implemented");
        }
    }
    return true;
}

static bool valueEquality(VM* vm, AstNode* node, Value lhs, Value rhs, bool* result) {
    Type t = GET_TYPE(lhs);
    if (GET_TYPE(rhs) != t) *result = false;
    else switch (t) {
        case TYPE_NUMBER: {
            // todo: do something special for floats?
            *result = GET_NUMBER(lhs) == GET_NUMBER(rhs);
        } break;
        case TYPE_SYMBOL: {
            *result = GET_SYMBOL(lhs) == GET_SYMBOL(rhs);
        } break;
        case TYPE_STRING: {
            *result = strcmp(GET_STRING(lhs), GET_STRING(rhs)) == 0;
        } break;
        case TYPE_ODDBALL: {
            *result = GET_ODDBALL(lhs) == GET_ODDBALL(rhs);
        } break;
        case TYPE_CONTEXT: {
            // todo: store #_eq
            Symbol symEq = Symbol_find("_eq", 3);
            Value* pfn = Context_get(GET_CONTEXT(lhs), symEq);
            if (!pfn) {
                // todo: do we want to return unbound here or have default behaviour?
                //       default behaviour might be better for sort etc.
                //       (this affects valueCompare too)
                raiseUnbound2(vm, node, symEq, lhs);
                return false;
            } else {
                int oldStk = vm->stack->next;
                Stack_push(vm->stack, rhs);
                if (!evalCall(vm, node, *pfn, &lhs)) return false;
                if (vm->stack->next != oldStk + 1) {
                    raiseInvalid(vm, node, "_eq metamethod returned incorrect amount of values");
                    return false;
                }
                *result = isTruthy(Stack_pop(vm->stack));
            }
        } break;
        case TYPE_CLOSURE: {
            *result = false;
        } break;
        case TYPE_LIST: {
            Stack* llist = GET_LIST(lhs), * rlist = GET_LIST(rhs);
            if (llist == rlist) {
                *result = true;
            } else if (llist->next != rlist->next) {
                *result = false;
            } else {
                for (int i = 0; i < llist->next; i++) {
                    // todo: this will recurse infinitely if a list contains itself
                    //       should there be some max recursion depth for comparison?
                    if (!valueEquality(vm, node, llist->values[i], rlist->values[i], result)) return false;
                    if (!*result) break;
                }
            }
        } break;
        default: {
            assert(0 && "not implemented");
        }
    }
    return true;
}

static bool applyOperator(VM* vm, AstNode* node, Value lhs, int op) {
    if (vm->stack->next == 0) {
        raiseUnderflow(vm, node, 1);
        return false;
    }
    Value rhs = Stack_pop(vm->stack);
    switch (op) {
        case OPR_EQ: {
            bool result = false;
            if (!valueEquality(vm, node, lhs, rhs, &result)) return false;
            PUSH(FROM_BOOL(result));
        } break;
        case OPR_NEQ: {
            bool result = false;
            if (!valueEquality(vm, node, lhs, rhs, &result)) return false;
            PUSH(FROM_BOOL(!result));
        } break;
        case OPR_CMP: {
            int result = 0;
            if (!valueCompare(vm, node, lhs, rhs, &result)) return false;
            if (result < 0) result = -1;
            else if (result > 0) result = 1;
            PUSH(FROM_NUMBER(result));
        } break;
        case OPR_LT: {
            int result = 0;
            if (!valueCompare(vm, node, lhs, rhs, &result)) return false;
            PUSH(FROM_BOOL(result < 0));
        } break;
        case OPR_LTEQ: {
            int result = 0;
            if (!valueCompare(vm, node, lhs, rhs, &result)) return false;
            PUSH(FROM_BOOL(result <= 0));
        } break;
        case OPR_GT: {
            int result = 0;
            if (!valueCompare(vm, node, lhs, rhs, &result)) return false;
            PUSH(FROM_BOOL(result > 0));
        } break;
        case OPR_GTEQ: {
            int result = 0;
            if (!valueCompare(vm, node, lhs, rhs, &result)) return false;
            PUSH(FROM_BOOL(result >= 0));
        } break;
        default: {
            Type t = GET_TYPE(lhs);
            if (t == TYPE_NUMBER) {
                if (GET_TYPE(rhs) != TYPE_NUMBER) {
                    raiseType(vm, node, TYPE_NUMBER);
                    return false;
                }
                double a = GET_NUMBER(lhs), b = GET_NUMBER(rhs);
                switch (op) {
                    case OPR_ADD: {
                        PUSH(FROM_NUMBER(a + b));
                    } break;
                    case OPR_SUB: {
                        PUSH(FROM_NUMBER(a - b));
                    } break;
                    case OPR_MUL: {
                        PUSH(FROM_NUMBER(a * b));
                    } break;
                    case OPR_DIV: {
                        PUSH(FROM_NUMBER(a / b));
                    } break;
                    case OPR_POW: {
                        PUSH(FROM_NUMBER(pow(a, b)));
                    } break;
                    case OPR_MOD: {
                        PUSH(FROM_NUMBER(fmod(a, b)));
                    } break;
                    default: {
                        assert(0 && "not implemented");
                    }
                }
            } else if (t == TYPE_CONTEXT) {
                Value* pfn = Context_get(GET_CONTEXT(lhs), vm->symOps[op]);
                if (pfn) {
                    Stack_push(vm->stack, rhs);
                    if (!evalCall(vm, node, *pfn, &lhs)) return false;
                } else {
                    raiseUnbound2(vm, node, vm->symOps[op], lhs);
                    return false;
                }
            } else {
                // todo: this should probably be #type
                //       some raiseCustom function would be nice
                raiseInvalid(vm, node, "operator undefined on given type");
                return false;
            }
        }
    }
    // optimize special cases
    // if (GET_TYPE(lhs) == TYPE_NUMBER) {
    //     if (GET_TYPE(rhs) != TYPE_NUMBER) {
    //         raiseType(vm, node, TYPE_NUMBER);
    //         return false;
    //     }
    //     double a = GET_NUMBER(lhs), b = GET_NUMBER(rhs);
    //     switch (op) {
    //         case OPR_ADD: {
    //             PUSH(FROM_NUMBER(a + b));
    //         } break;
    //         case OPR_SUB: {
    //             PUSH(FROM_NUMBER(a - b));
    //         } break;
    //         case OPR_MUL: {
    //             PUSH(FROM_NUMBER(a * b));
    //         } break;
    //         case OPR_DIV: {
    //             PUSH(FROM_NUMBER(a / b));
    //         } break;
    //         case OPR_POW: {
    //             PUSH(FROM_NUMBER(pow(a, b)));
    //         } break;
    //         case OPR_MOD: {
    //             PUSH(FROM_NUMBER(fmod(a, b)));
    //         } break;
    //         case OPR_SHL: {
    //             PUSH(FROM_NUMBER((int) a << (int) b));
    //         } break;
    //         case OPR_SHR: {
    //             PUSH(FROM_NUMBER((int) a >> (int) b));
    //         } break;
    //         case OPR_EQ: {
    //             PUSH(FROM_BOOL(a == b));
    //         } break;
    //         case OPR_NEQ: {
    //             PUSH(FROM_BOOL(a != b));
    //         } break;
    //         case OPR_LT: {
    //             PUSH(FROM_BOOL(a < b));
    //         } break;
    //         case OPR_GT: {
    //             PUSH(FROM_BOOL(a > b));
    //         } break;
    //         case OPR_LTEQ: {
    //             PUSH(FROM_BOOL(a <= b));
    //         } break;
    //         case OPR_GTEQ: {
    //             PUSH(FROM_BOOL(a >= b));
    //         } break;
    //         default: {
    //             // todo: ideally all operations done above and can just
    //             // assert(0) for default case
    //             // maybe do this for all non-context types?
    //             Stack_push(vm->stack, rhs);
    //             goto fallback;
    //         }
    //     }
    // } else {
    //     fallback:;
    //     Value* pfn = Context_get(getContext(vm, lhs), vm->symOps[op]);
    //     if (pfn) {
    //         if (!evalCall(vm, node, *pfn, &lhs)) return false;
    //     } else {
    //         raiseUnbound2(vm, node, vm->symOps[op], lhs);
    //         return false;
    //     }
    // }
    return true;
}

// todo: if we make only false/nil be falsey then we can do stuff like:
// 'hello' .find('a') or! {throw! 'not found'}
static bool isTruthy(Value v) {
    switch (GET_TYPE(v)) {
        // case TYPE_NUMBER: return
        //     fpclassify(GET_NUMBER(v)) != FP_ZERO &&
        //     fpclassify(GET_NUMBER(v)) != FP_NAN;
        // case TYPE_STRING: return GET_STRING(v)[0] != 0;
        case TYPE_ODDBALL: return
            GET_ODDBALL(v) == 0 || GET_ODDBALL(v) == 2;
        default: return true;
    }
}

static bool evalAndPop(VM* vm, AstNode* node, Value* value) {
    if (!evalNode(vm, node)) {
        return false;
    }
    if (vm->stack->next == 0) {
        raiseUnderflow(vm, node, 1); // todo: this node is wrong i think
        return false;
    }
    *value = Stack_pop(vm->stack);
    return true;
}

static bool evalSpecial(VM* vm, AstNode* node, int special, Value sub) {
    switch (special) {
        case SPC_MAP: {
            if (vm->stack->next == 0) return true;
            Stack* base = vm->stack;
            vm->stack = GC_MALLOC(sizeof(Stack));
            *vm->stack = (Stack) { .previous = base->previous };
            for (int i = 0; i < base->next; i++) {
                Stack_push(vm->stack, base->values[i]);
                if (!evalCall(vm, node, sub, NULL)) return false;
            }
        } break;
        case SPC_FOLD: {
            while (vm->stack->next > 1) {
                if (!evalCall(vm, node, sub, NULL)) return false;
            }
        } break;
        case SPC_FILTER: {
            if (vm->stack->next == 0) return true;
            Stack* base = vm->stack;
            vm->stack = GC_MALLOC(sizeof(Stack));
            *vm->stack = (Stack) { .previous = base->previous };
            for (int i = 0; i < base->next; i++) {
                Stack_push(vm->stack, base->values[i]);
                if (!evalCall(vm, node, sub, NULL)) return false;
                if (vm->stack->next == 0) {
                    raiseUnderflow(vm, node, 1);
                    return false;
                }
                if (isTruthy(Stack_pop(vm->stack))) {
                    Stack_push(vm->stack, base->values[i]);
                }
            }
        } break;
        case SPC_ZIP: {
            Stack* newStack = GC_MALLOC(sizeof(Stack));
            *newStack = (Stack) { .previous = vm->stack->previous };
            while (vm->stack->next > 0) {
                if (!evalCall(vm, node, sub, NULL)) return false;
                if (vm->stack->next == 0) {
                    raiseUnderflow(vm, node, 1);
                    return false;
                }
                Stack_push(newStack, Stack_pop(vm->stack));
            }
            Stack_reverse(newStack);
            vm->stack = newStack;
        } break;
        case SPC_IS: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value lhs = Stack_pop(vm->stack);
            bool result;
            if (GET_TYPE(lhs) != GET_TYPE(sub)) {
                result = false;
            } else switch (GET_TYPE(lhs)) {
                case TYPE_NUMBER:
                    result = GET_NUMBER(lhs) == GET_NUMBER(sub); break;
                case TYPE_SYMBOL:
                    result = GET_SYMBOL(lhs) == GET_SYMBOL(sub); break;
                case TYPE_STRING:
                    result = strcmp(GET_STRING(lhs), GET_STRING(sub)) == 0;
                    break;
                case TYPE_ODDBALL:
                    result = GET_ODDBALL(lhs) == GET_ODDBALL(sub); break;
                case TYPE_CONTEXT:
                    result = GET_CONTEXT(lhs) == GET_CONTEXT(sub); break;
                case TYPE_CLOSURE:
                    result = GET_CLOSURE(lhs) == GET_CLOSURE(sub); break;
                // todo: lists? blobs?
                default: result = false;
            }
            Stack_push(vm->stack, result ? VAL_TRUE : VAL_FALSE);
        } break;
        case SPC_AS: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value lhs = vm->stack->values[vm->stack->next - 1];
            bool subIsNil = sub.tag == TYPE_ODDBALL && GET_ODDBALL(sub) == 3;
            if (!subIsNil && GET_TYPE(sub) != TYPE_CONTEXT) {
                raiseType(vm, node, TYPE_CONTEXT);
                return false;
            }
            if (GET_TYPE(lhs) != TYPE_CONTEXT) {
                raiseType(vm, node, TYPE_CONTEXT);
                return false;
            }
            GET_CONTEXT(lhs)->parent = subIsNil ? NULL : GET_CONTEXT(sub);
        } break;
        case SPC_TO: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value lhs = Stack_pop(vm->stack);
            if (GET_TYPE(sub) != TYPE_NUMBER) {
                raiseType(vm, node, TYPE_NUMBER);
                return false;
            }
            if (GET_TYPE(lhs) != TYPE_NUMBER) {
                raiseType(vm, node, TYPE_NUMBER);
                return false;
            }
            double first = GET_NUMBER(lhs);
            double last = GET_NUMBER(sub);
            if (first < last) {
                for (double i = first; i < last; i++) {
                    Stack_push(vm->stack, FROM_NUMBER(i));
                }
            } else {
                for (double i = first; i > last; i--) {
                    Stack_push(vm->stack, FROM_NUMBER(i));
                }
            }
            Stack_push(vm->stack, FROM_NUMBER(last));
        } break;
        case SPC_DOT: {
            if (GET_TYPE(sub) != TYPE_NUMBER) {
                raiseType(vm, node, TYPE_NUMBER);
                return false;
            }
            int n = (int) GET_NUMBER(sub);
            if (!vm->stack->previous) {
                raiseUnderflow(vm, node, -1);
                return false;
            } else if (Stack_size(vm->stack->previous) < n) {
                raiseUnderflow(vm, node, n);
                return false;
            } else if (n < 0) {
                raiseInvalid(vm, node, "dot amount must be non-negative");
                return false;
            }
            Stack_move(vm->stack->previous, vm->stack, n);
        } break;
        case SPC_JOIN: {
            // todo: define on vm
            Symbol sJoin = Symbol_find("_join", 5);
            Value* pfn = Context_get(getContext(vm, sub), sJoin);
            if (pfn) {
                if (!evalCall(vm, node, *pfn, &sub)) return false;
            } else {
                raiseUnbound2(vm, node, sJoin, sub);
                return false;
            }
        } break;
        case SPC_REPEAT: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value v = Stack_pop(vm->stack);
            if (GET_TYPE(v) != TYPE_NUMBER) {
                raiseType(vm, node, TYPE_NUMBER);
                return false;
            }
            int count = GET_NUMBER(v);
            for (int i = 0; i < count; i++) {
                if (!evalCall(vm, node, sub, NULL)) return false;
            }
        } break;
        case SPC_WITH: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value v = Stack_pop(vm->stack);
            // todo: define on vm
            Symbol sWith = Symbol_find("_with", 5);
            Value* pfn = Context_get(getContext(vm, v), sWith);
            if (pfn) {
                Stack_push(vm->stack, sub);
                if (!evalCall(vm, node, *pfn, &sub)) return false;
            } else {
                raiseUnbound2(vm, node, sWith, sub);
                return false;
            }
        } break;
        case SPC_CATCH: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value body = Stack_pop(vm->stack);
            if (!evalCall(vm, node, body, NULL)) {
                Context* ex = Context_create(vm->exProto);
                Context_bind(ex, vm->symKey, FROM_SYMBOL(vm->exSymbol));
                Context_bind(ex, vm->symMessage, FROM_STRING(vm->exMessage));
                Context_bind(ex, vm->symTrace, FROM_LIST(genTraceList(vm)));
                Stack_push(vm->stack, FROM_CONTEXT(ex));
                if (!evalCall(vm, node, sub, NULL)) return false;
            }
        } break;
        case SPC_AND: case SPC_OR: {
            if (vm->stack->next == 0) {
                raiseUnderflow(vm, node, 1);
                return false;
            }
            Value body = vm->stack->values[vm->stack->next-1];
            if (special == SPC_OR ? !isTruthy(body) : isTruthy(body)) {
                vm->stack->next--;
                if (!evalCall(vm, node, sub, NULL)) return false;
            }
        } break;
        default: assert(0 && "not impl");
    }
    return true;
}

void raiseUnbound(VM* vm, AstNode* node, Symbol sym) {
    vm->exSymbol = vm->symExs[0];
    vm->exMessage = gc_sprintf("key %s unbound", Symbol_repr(sym));
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = node != NULL;
    traceNode(vm, node);
}

// todo: try and use this version more?
void raiseUnbound2(VM* vm, AstNode* node, Symbol sym, Value value) {
    vm->exSymbol = vm->symExs[0];
    vm->exMessage = gc_sprintf("key %s unbound in %s", Symbol_repr(sym),
        Value_repr(value, 1));
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = node != NULL;
    traceNode(vm, node);
}

void raiseUnderflow(VM* vm, AstNode* node, int n) {
    vm->exSymbol = vm->symExs[1];
    if (n == -1) vm->exMessage = "metastack underflow";
    else if (n == 1) vm->exMessage = "expected 1 value on stack";
    else vm->exMessage = gc_sprintf("expected %d values on stack", n);
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = node != NULL;
    traceNode(vm, node);
}

void raiseType(VM* vm, AstNode* node, Type type) {
    vm->exSymbol = vm->symExs[2];
    // todo: name types
    vm->exMessage = gc_sprintf("expected value of type %d", type);
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = node != NULL;
    traceNode(vm, node);
}

void raiseInvalid(VM* vm, AstNode* node, const char* msg) {
    vm->exSymbol = vm->symExs[3];
    if (msg) vm->exMessage = gc_sprintf("invalid operation (%s)", msg);
    else vm->exMessage = "invalid operation";
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = node != NULL;
    traceNode(vm, node);
}

void raiseInternal(VM* vm, const char* msg) {
    vm->exSymbol = vm->symExs[4];
    if (msg) vm->exMessage = gc_sprintf("internal error (%s)", msg);
    else vm->exMessage = "internal error";
    vm->exTraceFirst = NULL;
    vm->exSourceHasTrace = false;
}

static void traceNode(VM* vm, AstNode* node) {
    if (!node) return;
    
    // omit redundant frames from being emitted in traces
    // specifically this is frames like 'then {...}' which are within
    // a single function
    if (vm->exTraceFirst) {
        switch (node->kind) {
            case AST_THEN_ELSE:
            case AST_UNTIL_DO: {
                // todo: no way to tell if left or right child caused error
                // so require that both are closures to omit trace
                // possible resolution: somehow annoted previous trace with
                // direction taken?
                bool omitTrace = true;
                if (node->sub && node->sub->kind != AST_CLOSURE) omitTrace = false;
                if (node->as_node && node->as_node->kind != AST_CLOSURE) omitTrace = false;
                if (omitTrace) return;
            } break;
            case AST_SPECIAL: {
                if (node->sub->kind == AST_CLOSURE) return;
            } break;
            default: break;
        }
    }

    // allocate a new trace node and insert at end of list
    ExceptionTrace* trace = GC_MALLOC(sizeof(ExceptionTrace));
    *trace = (ExceptionTrace) {};
    trace->module = node->module;
    trace->range = node->pos;
    if (vm->exTraceFirst) {
        vm->exTraceLast->next = trace;
    } else {
        vm->exTraceFirst = trace;
    }
    vm->exTraceLast = trace;
}

static void traceNative(VM* vm, NativeClosure* nc) {
    ExceptionTrace* trace = GC_MALLOC(sizeof(ExceptionTrace));
    *trace = (ExceptionTrace) {};
    trace->module = nc->module;
    trace->symbol = nc->symbolName;
    if (vm->exTraceFirst) {
        vm->exTraceLast->next = trace;
    } else {
        vm->exTraceFirst = trace;
    }
    vm->exTraceLast = trace;
}

void VM_dump(VM* vm) {
    if (vm->stack->next == 0) return;
    printf("\033[32m");
    for (int i = 0; i < vm->stack->next; i++) {
        if (i) putchar(' ');
        printf("%s", Value_repr(vm->stack->values[i], 1));
    }
    printf("\033[0m\n");
}
