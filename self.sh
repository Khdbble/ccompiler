#!/bin/bash
set -e

TMP=$1
CC=$2
OUTPUT=$3

rm -rf $TMP
mkdir -p $TMP

nsc() {
    $CC -Iinclude -I/usr/local/include -I/usr/include \
        -I/usr/include/linux -I/usr/include/x86_64-linux-gnu \
        -o $TMP/${1%.c}.s src/$1
    gcc -c -o $TMP/${1%.c}.o $TMP/${1%.c}.s
}

cc() {
    gcc -c -o $TMP/${1%.c}.o $1
}

cp src/nsc.h ./nsc.h
nsc main.c
nsc type.c
nsc parser.c
nsc codegen.c
nsc tokenizer.c
nsc preprocessor.c

(cd $TMP; gcc -static -o ../$OUTPUT *.o)