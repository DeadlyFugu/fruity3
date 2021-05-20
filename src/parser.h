#pragma once
#include "common.h"
#include "symbols.h"
#include "module.h"

typedef struct sSourceRange SourceRange;
typedef struct sBlock Block;
typedef struct sParserError ParserError;
typedef struct sParser Parser;
typedef struct sAstChainElem AstChainElem;
typedef struct sAstNode AstNode;

typedef enum AstKind {
    AST_NUMBER, AST_SYMBOL, AST_STRING, AST_ODDBALL,
    AST_CLOSURE, AST_OBJECT,
    AST_CALLV, AST_GETV, AST_SETV, AST_BINDV,
    AST_HASV, AST_REFV,
    AST_PREBIND, AST_PRECALL, AST_PRECALL_BARE, AST_OPERATOR,
    AST_ARGUMENT,
    AST_GROUP,
    AST_DOTS,
    AST_THEN_ELSE, AST_UNTIL_DO,
    AST_SPECIAL, AST_PRIMITIVE, AST_IMPORT, AST_THIS, AST_SIGBIND
} AstKind;

typedef enum TokenKind {
    TOK_NUMBER, TOK_HASH_IDENT, TOK_STRING,
    TOK_TRUE, TOK_FALSE, TOK_DEFAULT, TOK_NIL,
    TOK_LBRACE, TOK_RBRACE, TOK_COLON_LBRACE,
    TOK_CHAIN, TOK_DOLLAR_CHAIN, TOK_GT_CHAIN,
    TOK_GT_GT_CHAIN, TOK_QUESTION_CHAIN, TOK_AND_CHAIN,
    TOK_CHAIN_COLON, TOK_CHAIN_LPAREN, TOK_CHAIN_BANG,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_EQ, TOK_BANG_EQ, TOK_LT, TOK_MT, TOK_LT_EQ, TOK_MT_EQ,
    TOK_HAT, TOK_PERCENT, TOK_LT_LT, TOK_GT_GT, TOK_AND, TOK_PIPE,
    TOK_PLUS_PLUS, TOK_MINUS_MINUS, TOK_STAR_STAR, TOK_SLASH_PERCENT,
    TOK_LT_GT,
    TOK_AT_IDENT,
    TOK_LPAREN, TOK_RPAREN,
    TOK_DOTS,
    TOK_THEN, TOK_ELSE, TOK_UNTIL, TOK_DO,
    TOK_MAP, TOK_FOLD, TOK_FILTER, TOK_ZIP,
    TOK_IS, TOK_AS, TOK_TO, TOK_DOT,
    TOK_JOIN, TOK_REPEAT, TOK_WITH, TOK_CATCH,
    TOK_AND_KEYWORD, TOK_OR,
    TOK_BACKSLASH_IDENT, TOK_IMPORT, TOK_THIS, TOK_EQ_GT,
    TOK_ERROR
} TokenKind;

typedef enum SpecialKind {
    SPC_MAP, SPC_FOLD, SPC_FILTER, SPC_ZIP,
    SPC_IS, SPC_AS, SPC_TO, SPC_DOT,
    SPC_JOIN, SPC_REPEAT, SPC_WITH, SPC_CATCH,
    SPC_AND, SPC_OR
} SpecialKind;

// todo: probably go back to the more flexible any 3 symbol operators
typedef enum OperatorKind {
    OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV,
    OPR_EQ, OPR_NEQ, OPR_LT, OPR_GT, OPR_LTEQ, OPR_GTEQ,
    OPR_POW, OPR_MOD, OPR_SHL, OPR_SHR, OPR_AND, OPR_OR,
    OPR_ADDADD, OPR_SUBSUB, OPR_MULMUL, OPR_DIVMOD,
    OPR_CMP
} OperatorKind;

struct sSourceRange {
    u32 begin, end;
};

struct sParserError {
    const char* message;
    SourceRange range;
    ParserError* next;
};

struct sParser {
    ParserError* firstError;
    ParserError* lastError;
    ModuleInfo* moduleInfo;

    // -- tokenizer output --
    int tokenCount;
    SourceRange* tokens;

    // -- token classifier --
    TokenKind* tokenKinds;

    // -- parser state --
    int nextToken;
};

struct sBlock {
    // u32* instrs;
    // u32* data;
    // SourceRange* mapping;
    AstNode* first;
    ModuleInfo* info;
};

struct sAstChainElem {
    Symbol symbol;
    AstChainElem* next;
};

struct sAstNode {
    AstKind kind;
    AstNode* next;
    AstNode* sub;
    union {
        int as_int;
        double as_number;
        Symbol as_symbol;
        const char* as_string;
        AstNode* as_node; // else/do sub
        AstChainElem* as_chain;
    };
    SourceRange pos;
    ModuleInfo* module;
};

// What if instead we have two-layer parse?
// First produces AST, second produces bytecode
// Both are kept - AST used for disasm, bytecode used for eval

// Parse string into list of tokens.
void fpTokenize(Parser* parser);

// Determine kind of each token
void fpClassifyTokens(Parser* parser);

// Parse string into executable block.
Block* fpParse(ModuleInfo* moduleInfo);

// Print the contents of a block for debug purposes.
void fpDumpBlock(Block* block);

// Print the contents of a node for debug purposes.
void fpDumpAst(AstNode* node, int depth);
