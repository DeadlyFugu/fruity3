#include "stack.h"
#include <assert.h>
#include <gc/gc.h>

void Stack_reserve(Stack* stack, int n) {
    int cap = stack->capacity;
    int oldCap = cap;
    if (cap == 0) cap = 8;
    while (stack->next + n > cap) cap *= 2;
    if (stack->capacity != cap) {
        stack->capacity = cap;
        int size = sizeof(Value) * cap;
        if (size < 20 * 1024) {
            stack->values = GC_REALLOC(stack->values, size);
        } else {
            void* newStack = GC_malloc_ignore_off_page(size);
            memcpy(newStack, stack->values, oldCap * sizeof(Value));
            GC_FREE(stack->values);
            stack->values = newStack;
        }
    }
}

void Stack_push(Stack* stack, Value v) {
    if (stack->next == stack->capacity) Stack_reserve(stack, 1);
    stack->values[stack->next++] = v;
}

Value Stack_pop(Stack* stack) {
    assert(stack->next > 0);
    return stack->values[--stack->next];
}

int Stack_size(Stack* stack) {
    return stack->next;
}

void Stack_move(Stack* from, Stack* to, int n) {
    // todo: if to is empty and from is all, can be done without memcpy
    assert(from->next >= n && n >= 0);
    from->next -= n;
    Stack_reserve(to, n);
    memcpy(
        &to->values[to->next],
        &from->values[from->next], sizeof(Value) * n);
    to->next += n;
}

void Stack_reverse(Stack* stack) {
    for (int i = 0; i < stack->next / 2; i++) {
        int other = stack->next - i - 1;
        Value tmp = stack->values[i];
        stack->values[i] = stack->values[other];
        stack->values[other] = tmp;
    }
}
