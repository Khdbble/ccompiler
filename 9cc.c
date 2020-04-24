#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// enum for types of tokens
typedef enum {
    TK_RESERVED,  // symbol token
    TK_NUM,       // integer token
    TK_EOF,       // end of file token
} TokenKind;

typedef struct Token Token;

// data structure for Token
struct Token {
    TokenKind kind;  // types of token
    Token *next;     // next token
    int val;         // value (it kind == TK_NUM)
    char *str;       // token strings
};

// current token
Token *token;

// function for reporting error
// it takes the same arguments as printf does
void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// this proceeds to the next token if it is the expected symbol
// and returns true; else returns false
bool consume(char op) {
    if (token->kind != TK_RESERVED || token->str[0] != op)
        return false;
    token = token->next;
    return true;
}

// it proceeds to the next token if it is the expected symbol
// else reports error
void expect(char op) {
    if (token->kind != TK_RESERVED || token->str[0] != op)
        error("unexpected input: '%c'", op);
    token = token->next;
}

// it proceeds to the next token and returns its value
// if it is the integer token; else reports error
int expect_number() {
    if (token->kind != TK_NUM)
        error("non integer input");
    int val = token->val;
    token = token->next;
    return val;
}

bool at_eof() {
    return token->kind == TK_EOF;
}

// creates new token and append it to the current token
Token *new_token(TokenKind kind, Token *cur, char *str) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str = str;
    cur->next = tok;
    return tok;
}

// tokenizes the input string p and returns the token
Token *tokenize(char *p) {
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while (*p) {
        // skip the space
        if (isspace(*p)) {
            p++;
            continue;
        }

        if (*p == '+' || *p == '-') {
            cur = new_token(TK_RESERVED, cur, p++);
            continue;
        }

        if (isdigit(*p)) {
            cur = new_token(TK_NUM, cur, p);
            cur->val = strtol(p, &p, 10);
            continue;
        }

        error("cannot tokenize the input");
    }

    new_token(TK_EOF, cur, p);
    return head.next;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "invalid number of the arguments\n");
        return 1;
    }

    // tokenize the input
    token = tokenize(argv[1]);

    // output the beginning of the assembly
    printf(".intel_syntax noprefix\n");
    printf(".global main\n");
    printf("main:\n");

    // check if the very first token is number
    // then output the first "mov" instruction
    printf("  mov rax, %d\n", expect_number());

    // outputs the assembly commands
    // while consuming the string of tokens made out of "+ <integer>" or "- <integer>"
    while (!at_eof()) {
        if (consume('+')) {
            printf("  add rax, %d\n", expect_number());
            continue;
        }

        expect('-');
        printf("  sub rax, %d\n", expect_number());
    }

    printf("  ret\n");
    return 0;
}