#include "nsc.h"

bool opt_E;
static char *input_file;

static void usage(void) {
    fprintf(stderr, "chibicc [ -I<path> ] <file>\n");
    exit(1);
}

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            usage();

        if (!strcmp(argv[i], "-E")) {
            opt_E = true;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0')
            error("unknown argument: %s", argv[i]);

        input_file = argv[i];
    }

    if (!input_file)
        error("no input files");
}

static void print_tokens(Token *tok) {
    int line = 1;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (line > 1 && tok->at_bol)
            printf("\n");
        if (tok->has_space && !tok->at_bol)
            printf(" ");
        printf("%.*s", tok->len, tok->loc);
        line++;
    }
    printf("\n");
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    // Tokenize and parse.
    Token *tok = tokenize_file(input_file);
    if (!tok)
        error("%s: %s", input_file, strerror(errno));

    tok = preprocess(tok);

    if (opt_E) {
        print_tokens(tok);
        exit(0);
    }

    Program *prog = parse(tok);

    // Assign offsets to local variables.
    for (Function *fn = prog->fns; fn; fn = fn->next) {
        // Besides local varaibles, callee-saved registers take 32 bytes
        // and the variable-argument save area takes 96 bytes in the stack.
        int offset = fn->is_variadic ? 128 : 32;

        for (Var *var = fn->locals; var; var = var->next) {
            offset = align_to(offset, var->align);
            offset += size_of(var->ty);
            var->offset = offset;
        }
        fn->stack_size = align_to(offset, 16);
    }

    // Traverse the AST to emit assembly.
    codegen(prog);

    return 0;
}