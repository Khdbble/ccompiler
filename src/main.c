#include "nsc.h"

int main(int argc, char **argv) {
    if (argc != 2)
        error("%s: invalid number of arguments", argv[0]);

    // Tokenize and parse.
    Token *tok = tokenize_file(argv[1]);
    Program *prog = parse(tok);

    // Assign offsets to local variables.
    for (Function *fn = prog->fns; fn; fn = fn->next) {
        int offset = 32;  // 32 for callee-saved registers
        for (Var *var = fn->locals; var; var = var->next) {
            offset = align_to(offset, var->ty->align);
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