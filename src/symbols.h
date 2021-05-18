#pragma once
#include "common.h"

Symbol Symbol_find(const char* symbol, int length);

const char* Symbol_name(Symbol s);
const char* Symbol_repr(Symbol s); // contains hash
