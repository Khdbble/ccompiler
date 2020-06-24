#include "nsc.h"

static void usage(void) {
    fprintf(stderr, "chibicc [ -I<path> ] <file>\n");
    exit(1);
}

int main(int argc, char **argv) {
    char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            usage();

        if (argv[i][0] == '-' && argv[i][1] != '\0')
            error("unknown argument: %s", argv[i]);

        filename = argv[i];
    }

    if (!filename)
        error("no input files");

    // Tokenize and parse.
    Token *tok = tokenize_file(filename);
    tok = preprocess(tok);
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

    // Emit a .file directive for the assembler.
    printf(".file 1 \"%s\"\n", argv[1]);

    // Traverse the AST to emit assembly.
    codegen(prog);

    return 0;
}