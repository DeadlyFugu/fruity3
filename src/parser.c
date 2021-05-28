#include "common.h"
#include "parser.h"
#include "fruity.h"

// todo: expose via header
extern const char* gc_sprintf(const char* fmt, ...);

static void parserError(Parser* parser, const char* msg, SourceRange range) {
    ParserError* err = GC_MALLOC(sizeof(ParserError));
    *err = (ParserError) {
        .message = msg,
        .range = range
    };
    if (parser->lastError) {
        parser->lastError->next = err;
    } else {
        parser->firstError = err;
    }
    parser->lastError = err;
}

#define YIELD_TOKEN(begin, end) do { \
    if (count == capacity) { \
        capacity *= 2; tokens = GC_REALLOC(tokens, sizeof(SourceRange) * capacity); \
    } \
    tokens[count++] = (SourceRange) { begin, end }; \
} while(0)

void fpTokenize(Parser* parser) {
    const char* source = parser->moduleInfo->source;
    int capacity = 16;
    int count = 0;
    SourceRange* tokens = GC_MALLOC(sizeof(SourceRange) * capacity);

    int begin = 0, curr = 0, state = 0;
    int column = 0, column_start = 0;

    int len = strlen(source);
    while (curr <= len) {
        int c = source[curr]; // TODO: unicode?
        if (c == 0) c = '\n';
        if (state == 0) {
            if (c == '"' || c == '\'') {
                begin = curr;
                state = c == '\'' ? 2 : 3;
            } else if (c == '/' && curr + 1 < len) {
                int n = source[curr + 1];
                if (n == '/') {
                    state = 4;
                } else if (n == '*') {
                    begin = curr;
                    state = 5;
                } else {
                    begin = curr;
                    state = 1;
                }
            } else if (c == '(' || c == '{' || c == ')' || c == '}') {
                YIELD_TOKEN(curr, curr+1);
            } else if (c != ' ' && c != '\n' && c != '\t') {
                begin = curr;
                state = 1;
            }
        } else if (state == 1) {
            if (c == ' ' || c == '\n' || c == '\t') {
                YIELD_TOKEN(begin, curr);
                state = 0;
            } else if (c == ')' || c == '}') {
                if (begin != curr) YIELD_TOKEN(begin, curr);
                YIELD_TOKEN(curr, curr + 1);
                state = 0;
            } else if (c == '(' || c == '{') {
                YIELD_TOKEN(begin, curr + 1);
                state = 0;
            }
        } else if (state == 2 || state == 3) {
            int delim = state == 2 ? '\'' : '\"';
            if (c == delim) {
                if (curr + 1 < len) {
                    int n = source[curr + 1];
                    // todo make common isWhitespace fn
                    if (n != ' ' && n != '\t' && n != '\n' && n != ')' && n != '}') {
                        parserError(parser,
                            "expected whitespace after string",
                            (SourceRange) { begin, curr + 1 });
                        return;
                    }
                }
                YIELD_TOKEN(begin, curr + 1);
                state = 0;
            } else if (c == '\\') {
                state += 4;
            } else if (c == '\n') {
                parserError(parser,
                    gc_sprintf("expected %c", delim),
                    (SourceRange) { begin, curr });
                return;
            }
        } else if (state == 6 || state == 7) {
            if (c == '\'' || c == '\"' || c == '\\' ||
                c == 'b' || c == 'f' || c == 'n' ||
                c == 'r' || c == 't' || c == '$') state -= 4;
            else if (c == 'u') {
                state = state == 6 ? 8 : 12;
            } else {
                parserError(parser,
                    "invalid escape sequence",
                    (SourceRange) { begin, curr });
                return;
            }
        } else if (state >= 8 && state <= 11) {
            // todo: validate unicode sequence?
            if (c == '\'' || c == '\"') {
                parserError(parser,
                    "invalid escape sequence",
                    (SourceRange) { begin, curr });
                return;
            }
            if (state < 11) state += 1; else state = 2;
        } else if (state >= 12 && state <= 15) {
            // todo: validate unicode sequence?
            if (c == '\'' || c == '\"') {
                parserError(parser,
                    "invalid escape sequence",
                    (SourceRange) { begin, curr });
                return;
            }
            if (state < 15) state += 1; else state = 3;
        } else if (state == 4) {
            if (c == '\n') {
                state = 0;
            }
        } else if (state == 5) {
            if (curr > 0 && c == '/' && source[curr - 1] == '*') {
                state = 0;
            }
        }
        curr += 1;
        if (c == '\n') { column += 1; column_start = curr; }
    }

    if (state == 5) {
        parserError(parser,
            "expected */",
            (SourceRange) { begin, curr });
        return;
    }

    parser->tokens = tokens;
    parser->tokenCount = count;
}

bool fpIsValidNumber(const char* str, int length);
bool fpIsValidIdent(const char* str, int length);
bool fpIsValidChain(const char* str, int length);

void fpClassifyTokens(Parser* parser) {
    parser->tokenKinds = GC_MALLOC(sizeof(TokenKind) * parser->tokenCount);
    for (int i = 0; i < parser->tokenCount; i++) {
        SourceRange* range = &parser->tokens[i];
        int length = range->end - range->begin;
        const char* lexeme = &parser->moduleInfo->source[range->begin];
        const char* lexemeEnd = &lexeme[length];
        TokenKind kind = TOK_ERROR;
        if (fpIsValidNumber(lexeme, length)) {
            kind = TOK_NUMBER;
        } else if (length == 4 && strncmp(lexeme, "true", length) == 0) {
            kind = TOK_TRUE;
        } else if (length == 5 && strncmp(lexeme, "false", length) == 0) {
            kind = TOK_FALSE;
        } else if (length == 7 && strncmp(lexeme, "default", length) == 0) {
            kind = TOK_DEFAULT;
        } else if (length == 3 && strncmp(lexeme, "nil", length) == 0) {
            kind = TOK_NIL;
        } else if (length == 4 && strncmp(lexeme, "then", length) == 0) {
            kind = TOK_THEN;
        } else if (length == 4 && strncmp(lexeme, "else", length) == 0) {
            kind = TOK_ELSE;
        } else if (length == 5 && strncmp(lexeme, "until", length) == 0) {
            kind = TOK_UNTIL;
        } else if (length == 2 && strncmp(lexeme, "do", length) == 0) {
            kind = TOK_DO;
        } else if (length == 3 && strncmp(lexeme, "map", length) == 0) {
            kind = TOK_MAP;
        } else if (length == 4 && strncmp(lexeme, "fold", length) == 0) {
            kind = TOK_FOLD;
        } else if (length == 6 && strncmp(lexeme, "filter", length) == 0) {
            kind = TOK_FILTER;
        } else if (length == 3 && strncmp(lexeme, "zip", length) == 0) {
            kind = TOK_ZIP;
        } else if (length == 2 && strncmp(lexeme, "is", length) == 0) {
            kind = TOK_IS;
        } else if (length == 2 && strncmp(lexeme, "as", length) == 0) {
            kind = TOK_AS;
        } else if (length == 2 && strncmp(lexeme, "to", length) == 0) {
            kind = TOK_TO;
        } else if (length == 3 && strncmp(lexeme, "dot", length) == 0) {
            kind = TOK_DOT;
        } else if (length == 4 && strncmp(lexeme, "join", length) == 0) {
            kind = TOK_JOIN;
        } else if (length == 6 && strncmp(lexeme, "repeat", length) == 0) {
            kind = TOK_REPEAT;
        } else if (length == 4 && strncmp(lexeme, "with", length) == 0) {
            kind = TOK_WITH;
        } else if (length == 5 && strncmp(lexeme, "catch", length) == 0) {
            kind = TOK_CATCH;
        } else if (length == 3 && strncmp(lexeme, "and", length) == 0) {
            kind = TOK_AND_KEYWORD;
        } else if (length == 2 && strncmp(lexeme, "or", length) == 0) {
            kind = TOK_OR;
        } else if (length == 6 && strncmp(lexeme, "import", length) == 0) {
            kind = TOK_IMPORT;
        } else if (length == 4 && strncmp(lexeme, "this", length) == 0) {
            kind = TOK_THIS;
        } else if (fpIsValidChain(lexeme, length)) {
            kind = TOK_CHAIN;
        } else if (length == 1) {
            switch (lexeme[0]) {
                case '+': kind = TOK_PLUS; break;
                case '-': kind = TOK_MINUS; break;
                case '*': kind = TOK_STAR; break;
                case '/': kind = TOK_SLASH; break;
                case '(': kind = TOK_LPAREN; break;
                case ')': kind = TOK_RPAREN; break;
                case '{': kind = TOK_LBRACE; break;
                case '}': kind = TOK_RBRACE; break;
                case '.': kind = TOK_DOTS; break;
                case '=': kind = TOK_EQ; break;
                case '<': kind = TOK_LT; break;
                case '>': kind = TOK_MT; break;
                case '^': kind = TOK_HAT; break;
                case '%': kind = TOK_PERCENT; break;
                // case '&': kind = TOK_AND; break;
                // case '|': kind = TOK_PIPE; break;
            }
        } else if (lexeme[0] == '\'') {
            kind = TOK_STRING;
        } else if (lexeme[0] == '#' && fpIsValidIdent(lexeme+1, length-1)) {
            kind = TOK_HASH_IDENT;
        } else if (lexeme[0] == '@' && fpIsValidIdent(lexeme+1, length-1)) {
            kind = TOK_AT_IDENT;
        } else if (lexeme[0] == '\\' && fpIsValidIdent(lexeme+1, length-1)) {
            kind = TOK_BACKSLASH_IDENT;
        } else if (lexeme[0] == '$' && fpIsValidChain(lexeme+1, length-1)) {
            kind = TOK_DOLLAR_CHAIN;
        } else if (lexeme[0] == '>' && fpIsValidChain(lexeme+1, length-1)) {
            kind = TOK_GT_CHAIN;
        } else if (length > 2 && lexeme[0] == '>' && lexeme[1] == '>' &&
            fpIsValidChain(lexeme+2, length-2)) {
            kind = TOK_GT_GT_CHAIN;
        } else if (lexeme[0] == '?' && fpIsValidChain(lexeme+1, length-1)) {
            kind = TOK_QUESTION_CHAIN;
        } else if (lexeme[0] == '&' && fpIsValidChain(lexeme+1, length-1)) {
            kind = TOK_AND_CHAIN;
        } else if (lexemeEnd[-1] == ':' && fpIsValidChain(lexeme, length-1)) {
            kind = TOK_CHAIN_COLON;
        } else if (lexemeEnd[-1] == '(' && fpIsValidChain(lexeme, length-1)) {
            kind = TOK_CHAIN_LPAREN;
        } else if (lexemeEnd[-1] == '!' && fpIsValidChain(lexeme, length-1)) {
            kind = TOK_CHAIN_BANG;
        } else if (length == 2) {
            if (strncmp(lexeme, ":{", 2) == 0) kind = TOK_COLON_LBRACE;
            else if (strncmp(lexeme, "!=", 2) == 0) kind = TOK_BANG_EQ;
            else if (strncmp(lexeme, "..", 2) == 0) kind = TOK_DOTS;
            else if (strncmp(lexeme, "<=", 2) == 0) kind = TOK_LT_EQ;
            else if (strncmp(lexeme, ">=", 2) == 0) kind = TOK_MT_EQ;
            else if (strncmp(lexeme, "=>", 2) == 0) kind = TOK_EQ_GT;
            else if (strncmp(lexeme, "<>", 2) == 0) kind = TOK_LT_GT;
            // else if (strncmp(lexeme, "<<", 2) == 0) kind = TOK_LT_LT;
            // else if (strncmp(lexeme, ">>", 2) == 0) kind = TOK_GT_GT;
            // else if (strncmp(lexeme, "++", 2) == 0) kind = TOK_PLUS_PLUS;
            // else if (strncmp(lexeme, "--", 2) == 0) kind = TOK_MINUS_MINUS;
            // else if (strncmp(lexeme, "**", 2) == 0) kind = TOK_STAR_STAR;
            // else if (strncmp(lexeme, "/%", 2) == 0) kind = TOK_SLASH_PERCENT;
        } else {
            bool isDots = true;
            for (int i = 0; i < length; i++) {
                if (lexeme[i] != '.') isDots = false;
            }
            if (isDots) kind = TOK_DOTS;
        }
        if (kind == TOK_ERROR) {
            parserError(parser, "invalid token", *range);
        }
        parser->tokenKinds[i] = kind;
    }
}

bool fpIsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_';
}

static const int ntDigit[10] = { 2, 2, 2, 2, 5, 5, 5, 8, 8, 8 };
static const int ntSign[10] =  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const int ntComma[10] = { 0, 0, 3, 0, 0, 6, 0, 0, 9, 0 };
static const int ntDot[10]   = { 4, 4, 4, 0, 0, 0, 0, 0, 0, 0 };
static const int ntExp[10]   = { 0, 0, 7, 0, 0, 7, 0, 0, 0, 0 };
bool fpIsValidNumber(const char* str, int length) {
    int state = 0;
    for (int i = 0; i < length; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') state = ntDigit[state];
        else if (c == '+' || c == '-') state = ntSign[state];
        else if (c == ',') state = ntComma[state];
        else if (c == '.') state = ntDot[state];
        else if (c == 'e' || c == 'E') state = ntExp[state];
        else return false;
        if (state == 0) return false;
    }
    return state == 2 || state == 5 || state == 8;
}
bool fpIsValidIdent(const char* str, int length) {
    for (int i = 0; i < length; i++) {
        char c = str[i];
        if (!fpIsIdentChar(c)) return false;
        else if (i == 0 && c >= '0' && c <= '9') return false;
    }
    return true;
}
bool fpIsValidChain(const char* str, int length) {
    int identLen = 0;
    for (int i = 0; i < length; i++) {
        char c = str[i];
        if (!fpIsIdentChar(c) && c != '.') return false;
        else if (identLen == 0 && c >= '0' && c <= '9') return false;
        else if (identLen == 0 && c == '.' && i != 0) return false;
        else if (c == '.') identLen = 0;
        else identLen++;
    }
    if (identLen == 0) return false;
    return true;
}

typedef struct sRangeInfo {
    int startLine, endLine;
    int startColumn, endColumn;
    const char* firstLine;
    int firstLineLength;
} RangeInfo;

static void findRangeInfo(
    const char* source, SourceRange range, RangeInfo* info) {
    
    // todo: utf8 aware
    int line = 1, column = 1, lineStart = 0;
    for (int i = 0; i < range.begin; i++) {
        if (source[i] == '\n') {
            line++;
            column = 1;
            lineStart = i+1;
        } else {
            column++;
        }
    }

    info->startLine = line;
    info->startColumn = column;
    info->firstLine = &source[lineStart];

    for (int i = range.begin; i < range.end; i++) {
        if (source[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }

    info->endLine = line;
    info->endColumn = column;

    int j = 0;
    for (; info->firstLine[j] && info->firstLine[j] != '\n'; j++);
    info->firstLineLength = j;
}

#include <sys/ioctl.h>
static void printTraceLine(RangeInfo* info, ModuleInfo* module) {
    struct winsize ws;
    int ioctlResult = ioctl(0, TIOCGWINSZ, &ws);
    int conWidth = ws.ws_col;
    if (ioctlResult) conWidth = 80;
    if (info->endColumn != info->startColumn) {
        int startOffset = info->startColumn - 1;
        printf("%.*s", startOffset, info->firstLine);
        int highlightWidth = info->firstLineLength - startOffset;
        if (info->startLine == info->endLine) {
            highlightWidth = info->endColumn - info->startColumn;
        }
        printf("\033[1;31m%.*s\033[0m",
            highlightWidth, &info->firstLine[startOffset]);
        int totalWidth = startOffset + highlightWidth;
        if (info->startLine == info->endLine) {
            int tailWidth = info->firstLineLength - totalWidth;
            printf("%.*s", tailWidth, &info->firstLine[totalWidth]);
            totalWidth += tailWidth;
        }
        if (module && module->filename) {
            char label[256];
            snprintf(label, 256, "(in %s:%d:%d)",
                module->filename, info->startLine, info->startColumn);
            int labelLen = strlen(label);
            if (totalWidth < conWidth && totalWidth + 1 + labelLen <= conWidth) {
                printf("%*s", conWidth - totalWidth, label);
            } else {
                printf("\n%*s", conWidth, label);
            }
        }
        printf("\n");
    }
}

static void dumpParseErrors(Parser* parser) {
    ParserError* err = parser->firstError;
    while (err) {
        SourceRange range = err->range;
        RangeInfo info;
        findRangeInfo(parser->moduleInfo->source, range, &info);
        printf("\033[31m#parser\033[0m %s    (in %s:%d:%d)\n",
            err->message,
            parser->moduleInfo->filename,
            info.startLine, info.startColumn);
        printTraceLine(&info, NULL);
        err = err->next;
    }
}

// todo: this should be in another .c file (vm?)
#include "vm.h"
void dumpError(VM* vm) {
    // print error message (includes symbol, message, and
    // optionally the location if within a native module)
    ExceptionTrace* trace = vm->exTraceFirst;
    // todo: exSourceHasTrace is probably redundant
    if (vm->exSourceHasTrace && trace && trace->module->native) {
        struct winsize ws;
        ioctl(0, TIOCGWINSZ, &ws);
        char label[256];
        RangeInfo info;
        const char* errSym = Symbol_repr(vm->exSymbol);
        printf("\033[31m%s\033[0m %s",
            errSym,
            vm->exMessage);
        snprintf(label, 256, "(in %s:%s)",
            trace->module->filename,
            trace->symbol);
        int labelLen = strlen(label);
        int msgLen = strlen(errSym) + strlen(vm->exMessage) + 1;
        if (msgLen < ws.ws_col && msgLen + 1 + labelLen <= ws.ws_col) {
            printf("%*s\n", ws.ws_col - msgLen, label);
        } else {
            printf("\n%*s\n", ws.ws_col, label);
        }
        trace = trace->next;
    } else {
        printf("\033[31m%s\033[0m %s\n",
            Symbol_repr(vm->exSymbol),
            vm->exMessage);
    }
    // print backtrace source lines
    if (!trace) {
        printf("\033[2m(trace not available)\033[0m\n");
    }
    int hiddenTraces = 0;
    while (trace) {
        if (trace->module->hideTrace && !vm->fullTrace) {
            hiddenTraces++;
        } else {
            if (hiddenTraces) {
                if (hiddenTraces == 1) {
                    printf("\033[2m(1 hidden trace)\033[0m\n");
                } else {
                    printf("\033[2m(%d hidden traces)\033[0m\n", hiddenTraces);
                }
                hiddenTraces = 0;
            }
            if (trace->module->native) {
                printf("(in %s:%s)\n",
                    trace->module->filename,
                    trace->symbol);
            } else {
                RangeInfo info;
                findRangeInfo(trace->module->source, trace->range, &info);
                printTraceLine(&info, trace->module);
            }
        }
        trace = trace->next;
    }
}

Stack* genTraceList(VM* vm) {
    Stack* list = GC_MALLOC(sizeof(Stack));
    *list = (Stack) {};
    ExceptionTrace* trace = vm->exTraceFirst;
    // todo: put these syms on vm or something
    Symbol symSource = Symbol_find("source", 6);
    Symbol symLine = Symbol_find("line", 4);
    Symbol symBegin = Symbol_find("begin", 5);
    Symbol symEnd = Symbol_find("end", 3);
    Symbol symNative = Symbol_find("native", 6);
    Symbol symHidden = Symbol_find("hidden", 6);
    while (trace) {
        Context* t = Context_create(NULL);
        if (!trace->module->native) {
            RangeInfo info;
            findRangeInfo(trace->module->source, trace->range, &info);
            const char* source = GC_strndup(info.firstLine, info.firstLineLength);
            Context_bind(t, symSource, FROM_STRING(source));
            Context_bind(t, symLine, FROM_NUMBER(info.startLine));
            Context_bind(t, symBegin, FROM_NUMBER(info.startColumn));
            int end = info.startLine == info.endLine ?
                info.endColumn : info.firstLineLength;
            Context_bind(t, symEnd, FROM_NUMBER(end));
        }
        // todo: we could avoid binding native hidden etc. if we just
        //       expose a Module object within fruity
        if (trace->module->native) Context_bind(t, symNative, VAL_TRUE);
        if (trace->module->hideTrace) Context_bind(t, symHidden, VAL_TRUE);
        Stack_push(list, FROM_CONTEXT(t));
        trace = trace->next;
    }
    return list;
}

bool fpExpectToken(Parser* parser, TokenKind kind, bool error);
bool fpHasNextUnit(Parser* parser);
AstNode* fpParseBody(Parser* parser, bool single);
AstChainElem* fpParseChain(Parser* parser, const char* start, int length);

Block* fpParse(ModuleInfo* moduleInfo) {
    Parser parser = { .moduleInfo = moduleInfo };
    fpTokenize(&parser);
    fpClassifyTokens(&parser);
    // todo: `./fp -e '\\'` somehow still runs despite parse error
    if (parser.firstError) {
        dumpParseErrors(&parser);
        return NULL;
    }
    // for (int i = 0; i < parser.tokenCount; i++) {
    //     int begin = parser.tokens[i].begin;
    //     int end = parser.tokens[i].end;
    //     const char* buf = moduleInfo->source;
    //     printf("TOKEN[%d - %d] '%.*s' %d\n",
    //         begin, end, end - begin, &buf[begin], parser.tokenKinds[i]);
    // }

    AstNode* root = NULL;
    if (fpHasNextUnit(&parser)) {
        root = fpParseBody(&parser, false);
    }

    if (parser.nextToken != parser.tokenCount) {
        parserError(&parser, "unexpected token",
            parser.tokens[parser.nextToken]);
    }
    if (parser.firstError) {
        dumpParseErrors(&parser);
        return NULL;
    }
    
    Block* block = GC_MALLOC(sizeof(Block));
    *block = (Block) { root, moduleInfo };
    return block;
}

AstNode* fpParseBody(Parser* parser, bool single) {
    if (parser->nextToken == parser->tokenCount) {
        parserError(parser,
            "unexpected end of input",
            parser->tokens[parser->nextToken-1]);
        return NULL;
    }
    SourceRange token = parser->tokens[parser->nextToken];
    int length = token.end - token.begin;
    TokenKind tkind = parser->tokenKinds[parser->nextToken];
    parser->nextToken++;
    const char* lexeme = &parser->moduleInfo->source[token.begin];
    AstNode* node = GC_MALLOC(sizeof(AstNode));
    *node = (AstNode) {
        .pos = token,
        .module = parser->moduleInfo
    };

    switch (tkind) {
        case TOK_NUMBER: {
            node->kind = AST_NUMBER;
            bool hasComma;
            for (int i = 0; i < length; i++) if (lexeme[i] == ',') hasComma = true;
            if (hasComma) {
                char* nocomma = GC_malloc(length);
                int i2 = 0;
                for (int i = 0; i < length; i++) {
                    if (lexeme[i] != ',') nocomma[i2++] = lexeme[i];
                }
                nocomma[i2] = 0;
                lexeme = nocomma;
            }
            node->as_number = atof(lexeme);
        } break;
        case TOK_HASH_IDENT: {
            node->kind = AST_SYMBOL;
            node->as_symbol = Symbol_find(lexeme + 1, length - 1);
        } break;
        case TOK_STRING: {
            node->kind = AST_STRING;
            node->as_string = fpStringUnescape(lexeme + 1, length - 2);
        } break;
        case TOK_TRUE: case TOK_FALSE: case TOK_DEFAULT: case TOK_NIL: {
            node->kind = AST_ODDBALL;
            node->as_symbol = tkind - TOK_TRUE;
        } break;
        case TOK_LBRACE: case TOK_COLON_LBRACE: {
            node->kind = tkind == TOK_LBRACE ? AST_CLOSURE : AST_OBJECT;
            if (fpHasNextUnit(parser)) {
                int sigBase = parser->nextToken;
                int lookahead = parser->nextToken;
                bool hasSigBinds = false;
                // todo: if sig contains non-chain token before => it treats it
                // as not a sig causing confusing error messages
                while (lookahead < parser->tokenCount) {
                    TokenKind kind = parser->tokenKinds[lookahead];
                    if (kind == TOK_EQ_GT) {
                        hasSigBinds = true;
                        break;
                    } else if (kind != TOK_CHAIN) break;
                    lookahead++;
                }
                if (hasSigBinds) parser->nextToken = lookahead + 1;
                if (fpHasNextUnit(parser)) {
                    node->sub = fpParseBody(parser, false);
                }
                if (hasSigBinds) {
                    for (int i = sigBase; i < lookahead; i++) {
                        SourceRange bind = parser->tokens[i];
                        const char* bindlex = &parser->moduleInfo->source[bind.begin];
                        // todo: validate lex is a valid symbol
                        Symbol bindSym = Symbol_find(bindlex, bind.end - bind.begin);
                        AstNode* bindNode = GC_MALLOC(sizeof(AstNode));
                        *bindNode = (AstNode) {
                            .kind = AST_SIGBIND,
                            .as_symbol = bindSym,
                            .pos = bind,
                            .module = parser->moduleInfo,
                            .next = node->sub
                        };
                        node->sub = bindNode;
                    }
                }
            }
            if (!fpExpectToken(parser, TOK_RBRACE, true)) return NULL;
        } break;
        case TOK_CHAIN: {
            node->kind = AST_CALLV;
            node->as_chain = fpParseChain(parser, lexeme, length);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_DOLLAR_CHAIN: {
            node->kind = AST_GETV;
            node->as_chain = fpParseChain(parser, lexeme + 1, length - 1);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_GT_CHAIN: {
            node->kind = AST_SETV;
            node->as_chain = fpParseChain(parser, lexeme + 1, length - 1);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_GT_GT_CHAIN: {
            node->kind = AST_BINDV;
            node->as_chain = fpParseChain(parser, lexeme + 2, length - 2);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_QUESTION_CHAIN: {
            node->kind = AST_HASV;
            node->as_chain = fpParseChain(parser, lexeme + 1, length - 1);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_AND_CHAIN: {
            node->kind = AST_REFV;
            node->as_chain = fpParseChain(parser, lexeme + 1, length - 1);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_CHAIN_COLON: {
            node->kind = AST_PREBIND;
            node->as_chain = fpParseChain(parser, lexeme, length - 1);
            node->sub = fpParseBody(parser, true);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_CHAIN_LPAREN: {
            node->kind = AST_PRECALL;
            node->as_chain = fpParseChain(parser, lexeme, length - 1);
            if (fpHasNextUnit(parser)) {
                node->sub = fpParseBody(parser, false);
            }
            if (!node->as_chain) return NULL;
            if (!fpExpectToken(parser, TOK_RPAREN, true)) return NULL;
        } break;
        case TOK_CHAIN_BANG: {
            node->kind = AST_PRECALL_BARE;
            node->as_chain = fpParseChain(parser, lexeme, length - 1);
            node->sub = fpParseBody(parser, true);
            if (!node->as_chain) return NULL;
        } break;
        case TOK_PLUS: case TOK_MINUS: case TOK_STAR: case TOK_SLASH:
        case TOK_EQ: case TOK_BANG_EQ: case TOK_LT: case TOK_MT:
        case TOK_LT_EQ: case TOK_MT_EQ:
        case TOK_HAT: case TOK_PERCENT: case TOK_LT_LT: case TOK_GT_GT:
        case TOK_AND: case TOK_PIPE: case TOK_PLUS_PLUS:
        case TOK_MINUS_MINUS: case TOK_STAR_STAR: case TOK_SLASH_PERCENT:
        case TOK_LT_GT: {
            node->kind = AST_OPERATOR;
            node->as_int = tkind - TOK_PLUS;
            node->sub = fpParseBody(parser, true);
        } break;
        case TOK_AT_IDENT: {
            node->kind = AST_ARGUMENT;
            node->as_symbol = Symbol_find(lexeme + 1, length - 1);
            node->sub = fpParseBody(parser, true);
        } break;
        case TOK_LPAREN: {
            node->kind = AST_GROUP;
            if (fpHasNextUnit(parser)) {
                node->sub = fpParseBody(parser, false);
            }
            if (!fpExpectToken(parser, TOK_RPAREN, true)) return NULL;
        } break;
        case TOK_DOTS: {
            node->kind = AST_DOTS;
            node->as_int = length;
        } break;
        case TOK_THEN: {
            node->kind = AST_THEN_ELSE;
            node->sub = fpParseBody(parser, true);
            if (fpExpectToken(parser, TOK_ELSE, false)) {
                node->as_node = fpParseBody(parser, true);
            }
        } break;
        case TOK_ELSE: {
            node->kind = AST_THEN_ELSE;
            node->as_node = fpParseBody(parser, true);
        } break;
        case TOK_UNTIL: {
            node->kind = AST_UNTIL_DO;
            node->sub = fpParseBody(parser, true);
            if (fpExpectToken(parser, TOK_DO, false)) {
                node->as_node = fpParseBody(parser, true);
            }
        } break;
        case TOK_DO: {
            node->kind = AST_UNTIL_DO;
            node->as_node = fpParseBody(parser, true);
        } break;
        case TOK_MAP: case TOK_FOLD: case TOK_FILTER: case TOK_ZIP:
        case TOK_IS: case TOK_AS: case TOK_TO: case TOK_DOT:
        case TOK_JOIN: case TOK_REPEAT: case TOK_WITH: case TOK_CATCH:
        case TOK_AND_KEYWORD: case TOK_OR: {
            node->kind = AST_SPECIAL;
            node->as_int = tkind - TOK_MAP;
            node->sub = fpParseBody(parser, true);
        } break;
        case TOK_IMPORT: {
            node->kind = AST_IMPORT;
            // if (!fpExpectToken(parser, TOK_CHAIN, true)) {
            //     printf("expected chain after import");
            //     return NULL;
            // }
            // SourceRange name = parser->tokens[parser->nextToken - 1];
            // int nameLen = name.end - name.begin;
            // node->as_string = GC_MALLOC(nameLen + 1);
            // memcpy(node->as_string,
            //     &parser->moduleInfo->source[name.begin], nameLen);
            // node->as_string[nameLen] = 0;
            // printf("!!%s", node->as_string);
            AstNode* chain = fpParseBody(parser, true);
            if (chain->kind != AST_CALLV) {
                parserError(parser, "expected chain after import", token);
                return NULL;
            }
            node->as_chain = chain->as_chain;
            if (fpExpectToken(parser, TOK_AS, false)) {
                node->sub = fpParseBody(parser, true);
                if (!node->sub) return NULL;
                if (node->sub->kind != AST_CALLV ||
                    node->sub->as_chain->next) {
                    parserError(parser,
                        "expected identifier after import as", token);
                    return NULL;
                }
            }
        } break;
        case TOK_THIS: {
            node->kind = AST_THIS;
        } break;
        default: {
            // todo: token name
            parserError(parser,
                gc_sprintf("unexpected token %d", tkind), token);
            return NULL;
        }
    }

    if (!single && fpHasNextUnit(parser)) {
        node->next = fpParseBody(parser, false);
    }

    return node;
}

AstChainElem* fpParseChain(Parser* parser, const char* start, int length) {
    AstChainElem* base = NULL;
    AstChainElem* last = NULL;
    int subStart = 0;
    // todo: pass range properly? (or get from current token in parser?)
    // actually this just should not be able to error, put error checking into
    // tokenizer? (can remove a bunch of if (!chain) return NULL then)
    // although errors for THIS mid-chain might be nicer from here...
    // (tokenizer would just not recognise)
    SourceRange range = { start - parser->moduleInfo->source };
    range.end = range.begin + length;
    if (start[0] == '.') {
        if (length == 1) {
            parserError(parser, "invalid chain", range);
            return NULL;
        }
        base = last = GC_MALLOC(sizeof(AstChainElem));
        *base = (AstChainElem) { -1, NULL };
        subStart++;
    }
    for (int i = subStart; i < length; i++) {
        if (start[i] != '.') continue;
        if (i == subStart) {
            parserError(parser, "invalid chain", range);
            return NULL;
        }
        AstChainElem* newElem = GC_MALLOC(sizeof(AstChainElem));
        newElem->symbol = Symbol_find(start + subStart, i - subStart);
        newElem->next = NULL;
        if (last) last->next = newElem;
        else base = newElem;
        last = newElem;
        subStart = i + 1;
    }
    // final elem
    if (length == subStart) {
        parserError(parser, "invalid chain", range);
        return NULL;
    }
    AstChainElem* newElem = GC_MALLOC(sizeof(AstChainElem));
    newElem->symbol = Symbol_find(start + subStart, length - subStart);
    newElem->next = NULL;
    if (last) last->next = newElem;
    else base = newElem;
    last = newElem;
    return base;
}

bool fpExpectToken(Parser* parser, TokenKind kind, bool error) {
    if (parser->nextToken == parser->tokenCount ||
        parser->tokenKinds[parser->nextToken] != kind) {
        if (error) {
            // todo: token name
            int tkIndex = parser->nextToken;
            if (tkIndex == parser->tokenCount) tkIndex--;
            parserError(parser, gc_sprintf("expected token %d", kind),
                parser->tokens[tkIndex]);
        }
        return false;
    }
    parser->nextToken++;
    return true;
}

bool fpHasNextUnit(Parser* parser) {
    if (parser->nextToken == parser->tokenCount ||
        parser->tokenKinds[parser->nextToken] == TOK_RBRACE ||
        parser->tokenKinds[parser->nextToken] == TOK_RPAREN) {
        return false;
    }
    return true;
}

void fpDumpBlock(Block* block) {
    printf("Block Dump\n");
    fpDumpAst(block->first, 1);
}

static const char* _AstKind_labels[] = {
    "AST_NUMBER", "AST_SYMBOL", "AST_STRING", "AST_ODDBALL",
    "AST_CLOSURE", "AST_OBJECT",
    "AST_CALLV", "AST_GETV", "AST_SETV", "AST_BINDV",
    "AST_HASV", "AST_REFV",
    "AST_PREBIND", "AST_PRECALL", "AST_PRECALL_BARE", "AST_OPERATOR",
    "AST_ARGUMENT",
    "AST_GROUP",
    "AST_DOTS",
    "AST_THEN_ELSE", "AST_UNTIL_DO",
    "AST_SPECIAL", "AST_PRIMITIVE", "AST_IMPORT", "AST_THIS", "AST_SIGBIND"
};

void fpDumpAst(AstNode* node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) putchar('\t');

    printf("%s", _AstKind_labels[node->kind]);

    switch (node->kind) {
        case AST_NUMBER:
            printf(" %lf", node->as_number);
            break;
        case AST_SYMBOL:
        case AST_ARGUMENT:
        case AST_SIGBIND:
            printf(" %s", Symbol_repr(node->as_symbol));
            break;
        case AST_ODDBALL:
        case AST_SPECIAL:
        case AST_PRIMITIVE:
        case AST_DOTS:
        case AST_OPERATOR:
            printf(" %d", node->as_int);
            break;
        case AST_STRING:
            printf(" %s", node->as_string);
            break;
        case AST_CALLV:
        case AST_GETV:
        case AST_SETV:
        case AST_BINDV:
        case AST_HASV:
        case AST_REFV:
        case AST_PREBIND:
        case AST_PRECALL:
        case AST_PRECALL_BARE:
        case AST_IMPORT: {
            AstChainElem* curr = node->as_chain;
            printf(" ");
            while (curr) {
                if (curr->symbol != (Symbol) -1) {
                    printf("%s", Symbol_name(curr->symbol));
                }
                if (curr->next) printf(".");
                curr = curr->next;
            }
        } break;
        default: break;
    }

    putchar('\n');

    fpDumpAst(node->sub, depth + 1);
    if (node->kind == AST_THEN_ELSE || node->kind == AST_UNTIL_DO) {
        if (node->as_node) {
            for (int i = 0; i < depth; i++) putchar('\t');
            printf("<ELSE>\n");
            fpDumpAst(node->as_node, depth + 1);
        }
    }
    fpDumpAst(node->next, depth);
}
