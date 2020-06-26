#include "nsc.h"

typedef struct MacroParam MacroParam;
struct MacroParam {
    MacroParam *next;
    char *name;
};

typedef struct MacroArg MacroArg;
struct MacroArg {
    MacroArg *next;
    char *name;
    Token *tok;
};

typedef struct Macro Macro;
struct Macro {
    Macro *next;
    char *name;
    bool is_objlike;  // Object-like or function-like
    MacroParam *params;
    Token *body;
    bool deleted;
};

// `#if` can be nested, so we use a stack to manage nested `#if`s.
typedef struct CondIncl CondIncl;
struct CondIncl {
    CondIncl *next;
    enum { IN_THEN,
           IN_ELIF,
           IN_ELSE } ctx;
    Token *tok;
    bool included;
};

typedef struct Hideset Hideset;
struct Hideset {
    Hideset *next;
    char *name;
};

static Macro *macros;
static Macro *file_macro;
static Macro *line_macro;
static CondIncl *cond_incl;

static Token *preprocess2(Token *tok);
static Macro *find_macro(Token *tok);

static bool is_hash(Token *tok) {
    return tok->at_bol && equal(tok, "#");
}

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(Token *tok) {
    if (tok->at_bol)
        return tok;
    warn_tok(tok, "extra token");
    while (tok->at_bol)
        tok = tok->next;
    return tok;
}

static Token *copy_token(Token *tok) {
    Token *t = malloc(sizeof(Token));
    *t = *tok;
    t->next = NULL;
    return t;
}

static Token *new_eof(Token *tok) {
    Token *t = copy_token(tok);
    t->kind = TK_EOF;
    t->len = 0;
    return t;
}

static Hideset *new_hideset(char *name) {
    Hideset *hs = calloc(1, sizeof(Hideset));
    hs->name = name;
    return hs;
}

static Hideset *hideset_union(Hideset *hs1, Hideset *hs2) {
    Hideset head = {};
    Hideset *cur = &head;

    for (; hs1; hs1 = hs1->next)
        cur = cur->next = new_hideset(hs1->name);
    cur->next = hs2;
    return head.next;
}

static bool hideset_contains(Hideset *hs, char *s, int len) {
    for (; hs; hs = hs->next)
        if (strlen(hs->name) == len && !strncmp(hs->name, s, len))
            return true;
    return false;
}

static Hideset *hideset_intersection(Hideset *hs1, Hideset *hs2) {
    Hideset head = {};
    Hideset *cur = &head;

    for (; hs1; hs1 = hs1->next)
        if (hideset_contains(hs2, hs1->name, strlen(hs1->name)))
            cur = cur->next = new_hideset(hs1->name);
    return head.next;
}

static Token *add_hideset(Token *tok, Hideset *hs) {
    Token head = {};
    Token *cur = &head;

    for (; tok; tok = tok->next) {
        Token *t = copy_token(tok);
        t->hideset = hideset_union(t->hideset, hs);
        cur = cur->next = t;
    }
    return head.next;
}

// Append tok2 to the end of tok1.
static Token *append(Token *tok1, Token *tok2) {
    if (!tok1 || tok1->kind == TK_EOF)
        return tok2;

    Token head = {};
    Token *cur = &head;

    for (; tok1 && tok1->kind != TK_EOF; tok1 = tok1->next)
        cur = cur->next = copy_token(tok1);
    cur->next = tok2;
    return head.next;
}

static Token *skip_cond_incl2(Token *tok) {
    while (tok->kind != TK_EOF) {
        if (is_hash(tok) &&
            (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
             equal(tok->next, "ifndef"))) {
            tok = skip_cond_incl2(tok->next->next);
            continue;
        }
        if (is_hash(tok) && equal(tok->next, "endif"))
            return tok->next->next;
        tok = tok->next;
    }
    return tok;
}

// Skip until next `#else`, `#elif` or `#endif`.
// Nested `#if` and `#endif` are skipped.
static Token *skip_cond_incl(Token *tok) {
    while (tok->kind != TK_EOF) {
        if (is_hash(tok) &&
            (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
             equal(tok->next, "ifndef"))) {
            tok = skip_cond_incl2(tok->next->next);
            continue;
        }

        if (is_hash(tok) &&
            (equal(tok->next, "elif") || equal(tok->next, "else") ||
             equal(tok->next, "endif")))
            break;
        tok = tok->next;
    }
    return tok;
}

// Double-quote a given string and returns it.
static char *quote_string(char *str) {
    int bufsize = 3;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            bufsize++;
        bufsize++;
    }

    char *buf = malloc(bufsize);
    char *p = buf;
    *p++ = '"';
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            *p++ = '\\';
        *p++ = str[i];
    }
    *p++ = '"';
    *p++ = '\0';
    return buf;
}

static Token *new_str_token(char *str, Token *tmpl) {
    char *buf = quote_string(str);
    return tokenize(tmpl->filename, tmpl->file_no, buf);
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(Token **rest, Token *tok) {
    Token head = {};
    Token *cur = &head;

    for (; !tok->at_bol; tok = tok->next)
        cur = cur->next = copy_token(tok);

    cur->next = new_eof(tok);
    *rest = tok;
    return head.next;
}

static Token *new_num_token(int val, Token *tmpl) {
    char *buf = malloc(20);
    sprintf(buf, "%d\n", val);
    return tokenize(tmpl->filename, tmpl->file_no, buf);
}

static Token *read_const_expr(Token **rest, Token *tok) {
    tok = copy_line(rest, tok);

    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // "defined(foo)" or "defined foo" becomes "1" if macro "foo"
        // is defined. Otherwise "0".
        if (equal(tok, "defined")) {
            Token *start = tok;
            bool has_paren = consume(&tok, tok->next, "(");

            if (tok->kind != TK_IDENT)
                error_tok(start, "macro name must be an identifier");
            Macro *m = find_macro(tok);
            tok = tok->next;

            if (has_paren)
                tok = skip(tok, ")");

            cur = cur->next = new_num_token(m ? 1 : 0, start);
            continue;
        }

        cur = cur->next = tok;
        tok = tok->next;
    }

    cur->next = tok;
    return head.next;
}

// Read and evaluate a constant expression.
static long eval_const_expr(Token **rest, Token *tok) {
    Token *expr = read_const_expr(rest, tok);
    expr = preprocess2(expr);

    // The standard requires we replace remaining non-macro
    // identifiers with "0" before evaluating a constant expression.
    // I don't know the rationale behind it, but I guess there's a
    // historical reason.
    for (Token *t = expr; t->kind != TK_EOF; t = t->next) {
        if (t->kind == TK_IDENT) {
            Token *next = t->next;
            *t = *new_num_token(0, t);
            t->next = next;
        }
    }

    Token *rest2;
    long val = const_expr(&rest2, expr);
    if (rest2->kind != TK_EOF)
        error_tok(rest2, "extra token");
    return val;
}

static CondIncl *push_cond_incl(Token *tok, bool included) {
    CondIncl *ci = calloc(1, sizeof(CondIncl));
    ci->next = cond_incl;
    ci->ctx = IN_THEN;
    ci->tok = tok;
    ci->included = included;
    cond_incl = ci;
    return ci;
}

static Macro *find_macro(Token *tok) {
    if (tok->kind != TK_IDENT)
        return NULL;

    for (Macro *m = macros; m; m = m->next)
        if (strlen(m->name) == tok->len && !strncmp(m->name, tok->loc, tok->len))
            return m->deleted ? NULL : m;
    return NULL;
}

static Macro *add_macro(char *name, bool is_objlike, Token *body) {
    Macro *m = calloc(1, sizeof(Macro));
    m->next = macros;
    m->name = name;
    m->is_objlike = is_objlike;
    m->body = body;
    macros = m;
    return m;
}

static MacroParam *read_macro_params(Token **rest, Token *tok) {
    MacroParam head = {};
    MacroParam *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");

        if (tok->kind != TK_IDENT)
            error_tok(tok, "expected an identifier");
        MacroParam *m = calloc(1, sizeof(MacroParam));
        m->name = strndup(tok->loc, tok->len);
        cur = cur->next = m;
        tok = tok->next;
    }
    *rest = tok->next;
    return head.next;
}

static void read_macro_definition(Token **rest, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(tok, "macro name must be an identifier");
    char *name = strndup(tok->loc, tok->len);
    tok = tok->next;

    if (!tok->has_space && equal(tok, "(")) {
        // Function-like macro
        MacroParam *params = read_macro_params(&tok, tok->next);
        Macro *m = add_macro(name, false, copy_line(rest, tok));
        m->params = params;
    } else {
        // Object-like macro
        add_macro(name, true, copy_line(rest, tok));
    }
}

static MacroArg *read_macro_arg_one(Token **rest, Token *tok) {
    Token head = {};
    Token *cur = &head;
    int level = 0;

    while (level > 0 || (!equal(tok, ",") && !equal(tok, ")"))) {
        if (tok->kind == TK_EOF)
            error_tok(tok, "premature end of input");

        if (equal(tok, "("))
            level++;
        else if (equal(tok, ")"))
            level--;

        cur = cur->next = copy_token(tok);
        tok = tok->next;
    }

    MacroArg *arg = calloc(1, sizeof(MacroArg));
    arg->tok = head.next;
    *rest = tok;
    return arg;
}

static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params) {
    Token *start = tok;
    tok = tok->next->next;

    MacroArg head = {};
    MacroArg *cur = &head;

    MacroParam *pp = params;
    for (; pp; pp = pp->next) {
        if (cur != &head)
            tok = skip(tok, ",");
        cur = cur->next = read_macro_arg_one(&tok, tok);
        cur->name = pp->name;
    }

    if (pp)
        error_tok(start, "too many arguments");
    skip(tok, ")");
    *rest = tok;
    return head.next;
}

// Macro arguments can be empty. For example, the second arugment
// of foo(a,,c) is the empty list of tokens. The following special
// pointer represents the empty list.
static Token *EMPTY = (Token *)-1;

static Token *find_arg(MacroArg *args, Token *tok) {
    for (MacroArg *ap = args; ap; ap = ap->next)
        if (tok->len == strlen(ap->name) && !strncmp(tok->loc, ap->name, tok->len))
            return ap->tok ? ap->tok : EMPTY;
    return NULL;
}

// Concatenates all tokens in `tok` and returns a new string.
static char *join_tokens(Token *tok, Token *end) {
    // Compute the length of the resulting token.
    int len = 1;
    for (Token *t = tok; t != end; t = t->next) {
        if (t != tok && t->has_space)
            len++;
        len += t->len;
    }

    char *buf = malloc(len);

    // Copy token texts.
    int pos = 0;
    for (Token *t = tok; t != end; t = t->next) {
        if (t != tok && t->has_space)
            buf[pos++] = ' ';
        strncpy(buf + pos, t->loc, t->len);
        pos += t->len;
    }
    buf[pos] = '\0';
    return buf;
}

// Concatenates all tokens in `arg` and returns a new string token.
// This function is used for the stringizing operator (#).
static Token *stringize(Token *hash, Token *arg) {
    // Create a new string token. We need to set some value to its
    // source location for error reporting function, so we use a macro
    // name token as a template.
    char *s = join_tokens(arg, NULL);
    return new_str_token(s, hash);
}

// Concatenate two tokens to create a new token.
static Token *paste(Token *lhs, Token *rhs) {
    // Paste the two tokens.
    char *buf = malloc(lhs->len + rhs->len + 1);
    sprintf(buf, "%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);

    // Tokenize the resulting string.
    Token *tok = tokenize(lhs->filename, lhs->file_no, buf);
    if (tok->next->kind != TK_EOF)
        error_tok(lhs, "pasting forms '%s', an invalid token", buf);
    return tok;
}

// Replace func-like macro parameters with given arguments.
static Token *subst(Token *tok, MacroArg *args) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        Token *arg = find_arg(args, tok);

        // If the current token is a macro parameter, replaces
        // it with actuals.
        if (arg) {
            tok = tok->next;

            // x##y becomes y if x is the empty argument list.
            if (arg == EMPTY && equal(tok, "##")) {
                tok = tok->next;
                continue;
            }

            if (arg != EMPTY)
                for (Token *t = arg; t; t = t->next)
                    cur = cur->next = copy_token(t);
            continue;
        }

        // Replace x##y with xy. LHS has already been macro-expanded and
        // added to `cur`.
        if (equal(tok, "##")) {
            tok = tok->next;
            Token *rhs = find_arg(args, tok);

            if (!rhs) {
                *cur = *paste(cur, tok);
                tok = tok->next;
                continue;
            }

            tok = tok->next;

            // x##y becomes x if y is the empty argument list.
            if (rhs == EMPTY)
                continue;

            *cur = *paste(cur, rhs);
            for (Token *t = rhs->next; t; t = t->next)
                cur = cur->next = copy_token(t);
            continue;
        }

        // "#" followed by a parameter is replaced with stringized actuals.
        if (equal(tok, "#")) {
            Token *arg = find_arg(args, tok->next);
            if (arg) {
                cur = cur->next = stringize(tok, arg);
                tok = tok->next->next;
                continue;
            }
        }

        // Handle non-macro token.
        cur = cur->next = copy_token(tok);
        tok = tok->next;
        continue;
    }

    return head.next;
}

static bool expand_macro(Token **rest, Token *tok) {
    if (hideset_contains(tok->hideset, tok->loc, tok->len))
        return false;

    Macro *m = find_macro(tok);
    if (!m)
        return false;

    // Object-like macro application
    if (m->is_objlike) {
        if (m == file_macro) {
            *rest = new_str_token(tok->filename, tok);
            (*rest)->next = tok->next;
            return true;
        }

        if (m == line_macro) {
            *rest = new_num_token(tok->line_no, tok);
            (*rest)->next = tok->next;
            return true;
        }

        Hideset *hs = hideset_union(tok->hideset, new_hideset(m->name));
        Token *body = add_hideset(m->body, hs);
        *rest = append(body, tok->next);
        return true;
    }

    // If a funclike macro token is not followed by an argument list,
    // treat it as a normal identifier.
    if (!equal(tok->next, "("))
        return false;

    // Function-like macro application
    Token *macro_token = tok;
    MacroArg *args = read_macro_args(&tok, tok, m->params);
    Token *rparen = tok;

    // Tokens that consist a func-like macro invocation may have different
    // hidesets, and if that's the case, it's not clear what the hideset
    // for the new tokens should be. We take the interesection of the
    // macro token and the closing parenthesis and use it as a new hideset
    // as explained in the Dave Prossor's algorithm.
    Hideset *hs = hideset_intersection(macro_token->hideset, rparen->hideset);
    hs = hideset_union(hs, new_hideset(m->name));

    Token *body = subst(m->body, args);
    body = add_hideset(body, hs);
    *rest = append(body, tok->next);
    return true;
}

// Returns a new string "dir/file".
static char *join_paths(char *dir, char *file) {
    char *buf = malloc(strlen(dir) + strlen(file) + 2);
    sprintf(buf, "%s/%s", dir, file);
    return buf;
}

// Returns true if a given file exists.
static bool file_exists(char *path) {
    struct stat st;
    return !stat(path, &st);
}

static char *search_include_paths(char *filename, Token *start) {
    // Search a file from the include paths.
    for (char **p = include_paths; *p; p++) {
        char *path = join_paths(*p, filename);
        if (file_exists(path))
            return path;
    }
    error_tok(start, "'%s': file not found", filename);
}

// Read an #include argument.
static char *read_include_path(Token **rest, Token *tok) {
    // Pattern 1: #include "foo.h"
    if (tok->kind == TK_STR) {
        // A double-quoted filename for #include is a special kind of
        // token, and we don't want to interpret any escape sequences in it.
        // For example, "\f" in "C:\foo" is not a formfeed character but
        // just two non-control characters, backslash and f.
        // So we don't want to use token->contents.
        Token *start = tok;
        char *filename = strndup(tok->loc + 1, tok->len - 2);
        *rest = skip_line(tok->next);

        if (file_exists(filename))
            return filename;
        return search_include_paths(filename, start);
    }

    // Pattern 2: #include <foo.h>
    if (equal(tok, "<")) {
        // Reconstruct a filename from a sequence of tokens between
        // "<" and ">".
        Token *start = tok;

        // Find closing ">".
        for (; !equal(tok, ">"); tok = tok->next)
            if (tok->kind == TK_EOF)
                error_tok(tok, "expected '>'");

        char *filename = join_tokens(start->next, tok);
        *rest = skip_line(tok->next);
        return search_include_paths(filename, start);
    }

    // Pattern 3: #include FOO
    // In this case FOO must be macro-expanded to either
    // a single string token or a sequence of "<" ... ">".
    if (tok->kind == TK_IDENT) {
        Token *tok2 = preprocess(copy_line(rest, tok));
        return read_include_path(&tok2, tok2);
    }

    error_tok(tok, "expected a filename");
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(Token *tok) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // If it is a macro, expand it.
        if (expand_macro(&tok, tok))
            continue;

        // Pass through if it is not a "#".
        if (!is_hash(tok)) {
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        Token *start = tok;
        tok = tok->next;

        if (equal(tok, "include")) {
            char *path = read_include_path(&tok, tok->next);
            Token *tok2 = tokenize_file(path);
            if (!tok2)
                error_tok(tok, "%s", strerror(errno));
            tok = append(tok2, tok);
            continue;
        }

        if (equal(tok, "define")) {
            read_macro_definition(&tok, tok->next);
            continue;
        }

        if (equal(tok, "undef")) {
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_tok(tok, "macro name must be an identifier");
            char *name = strndup(tok->loc, tok->len);
            tok = skip_line(tok->next);

            Macro *m = add_macro(name, true, NULL);
            m->deleted = true;
            continue;
        }

        if (equal(tok, "if")) {
            long val = eval_const_expr(&tok, tok->next);
            push_cond_incl(start, val);
            if (!val)
                tok = skip_cond_incl(tok);
            continue;
        }

        if (equal(tok, "ifdef")) {
            bool defined = find_macro(tok->next);
            push_cond_incl(tok, defined);
            tok = skip_line(tok->next->next);
            if (!defined)
                tok = skip_cond_incl(tok);
            continue;
        }

        if (equal(tok, "ifndef")) {
            bool defined = find_macro(tok->next);
            push_cond_incl(tok, !defined);
            tok = skip_line(tok->next->next);
            if (defined)
                tok = skip_cond_incl(tok);
            continue;
        }

        if (equal(tok, "elif")) {
            if (!cond_incl || cond_incl->ctx == IN_ELSE)
                error_tok(start, "stray #elif");
            cond_incl->ctx = IN_ELIF;

            if (!cond_incl->included && eval_const_expr(&tok, tok->next))
                cond_incl->included = true;
            else
                tok = skip_cond_incl(tok->next);
            continue;
        }

        if (equal(tok, "else")) {
            if (!cond_incl || cond_incl->ctx == IN_ELSE)
                error_tok(start, "stray #else");
            cond_incl->ctx = IN_ELSE;
            tok = skip_line(tok->next);

            if (cond_incl->included)
                tok = skip_cond_incl(tok);
            continue;
        }

        if (equal(tok, "endif")) {
            if (!cond_incl)
                error_tok(start, "stray #endif");
            cond_incl = cond_incl->next;
            tok = skip_line(tok->next);
            continue;
        }

        if (equal(tok, "error"))
            error_tok(tok, "");

        // `#`-only line is legal. It's called a null directive.
        if (tok->at_bol)
            continue;

        error_tok(tok, "invalid preprocessor directive");
    }

    cur->next = tok;
    return head.next;
}

static void define_macro(char *name, char *buf) {
    Token *tok = tokenize("(internal)", 1, buf);
    add_macro(name, true, tok);
}

static void init_macros(void) {
    // Define predefined macros
    define_macro("__chibicc__", "1");
    define_macro("_LP64", "1");
    define_macro("__ELF__", "1");
    define_macro("__LP64__", "1");
    define_macro("__SIZEOF_DOUBLE__", "8");
    define_macro("__SIZEOF_FLOAT__", "4");
    define_macro("__SIZEOF_INT__", "4");
    define_macro("__SIZEOF_LONG_DOUBLE__", "8");
    define_macro("__SIZEOF_LONG_LONG__", "8");
    define_macro("__SIZEOF_LONG__", "8");
    define_macro("__SIZEOF_POINTER__", "8");
    define_macro("__SIZEOF_PTRDIFF_T__", "8");
    define_macro("__SIZEOF_SHORT__", "2");
    define_macro("__SIZEOF_SIZE_T__", "8");
    define_macro("__STDC_HOSTED__", "1");
    define_macro("__STDC_ISO_10646__", "201103L");
    define_macro("__STDC_NO_ATOMICS__", "1");
    define_macro("__STDC_NO_COMPLEX__", "1");
    define_macro("__STDC_NO_THREADS__", "1");
    define_macro("__STDC_NO_VLA__", "1");
    define_macro("__STDC_UTF_16__", "1");
    define_macro("__STDC_UTF_32__", "1");
    define_macro("__STDC_VERSION__", "201112L");
    define_macro("__STDC__", "1");
    define_macro("__amd64", "1");
    define_macro("__amd64__", "1");
    define_macro("__gnu_linux__", "1");
    define_macro("__linux", "1");
    define_macro("__linux__", "1");
    define_macro("__unix", "1");
    define_macro("__unix__", "1");
    define_macro("__x86_64", "1");
    define_macro("__x86_64__", "1");
    define_macro("linux", "1");
    define_macro("__alignof__", "alignof");
    define_macro("__const__", "const");
    define_macro("__inline__", "inline");
    define_macro("__restrict", "restrict");
    define_macro("__restrict__", "restrict");
    define_macro("__signed__", "signed");
    define_macro("__typeof__", "typeof");
    define_macro("__volatile__", "volatile");

    file_macro = add_macro("__FILE__", true, NULL);
    line_macro = add_macro("__LINE__", true, NULL);
}

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
    init_macros();
    tok = preprocess2(tok);
    if (cond_incl)
        error_tok(cond_incl->tok, "unterminated conditional directive");
    convert_keywords(tok);
    return tok;
}