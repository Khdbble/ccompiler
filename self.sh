#!/bin/bash -x
set -e

TMP=tmp-self

mkdir -p $TMP

nsc() {
    cat <<EOF > $TMP/$1
typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *buf, long size);
int *__errno_location();
char *strerror(int errnum);
FILE *fopen(char *pathname, char *mode);
long fread(void *ptr, long size, long nmemb, FILE *stream);
int fclose(FILE *fp);
int feof(FILE *stream);
static void assert() {}
int strcmp(char *s1, char *s2);
EOF

    grep -v '^#' nsc.h >> $TMP/$1
    grep -v '^#' $1 >> $TMP/$1
    sed -i 's/\bbool\b/_Bool/g' $TMP/$1
    sed -i 's/\berrno\b/*__errno_location()/g' $TMP/$1
    sed -i 's/\btrue\b/1/g; s/\bfalse\b/0/g;' $TMP/$1
    sed -i 's/\bNULL\b/0/g' $TMP/$1
    sed -i 's/, \.\.\.//g' $TMP/$1

    ./nsc $TMP/$1 > $TMP/${1%.c}.s
    gcc -c -o $TMP/${1%.c}.o $TMP/${1%.c}.s
}

cc() {
    gcc -c -o $TMP/${1%.c}.o $1
}

nsc main.c
nsc type.c
cc parse.c
cc codegen.c
cc tokenize.c

gcc -static -o chibicc-stage2 $TMP/*.o