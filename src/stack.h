#pragma once
#include "common.h"
#include "value.h"

typedef struct sStack Stack;

struct sStack {
    Value* values;
    int next;
    int capacity;
    Stack* previous;
};

// todo: push/pop/size should be macros for perf

// Stack* Stack_create(void);
void Stack_reserve(Stack* stack, int n);
void Stack_push(Stack* stack, Value v);
Value Stack_pop(Stack* stack);
int Stack_size(Stack* stack);
void Stack_move(Stack* from, Stack* to, int n);
void Stack_reverse(Stack* stack);
