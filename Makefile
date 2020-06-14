CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)

nsc: $(OBJS)
		gcc -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): ./src/nsc.h

simpletest: nsc
		./nsc tests/tests.c > tmp.s
		echo 'int char_fn() { return 257; }' | gcc -xc -c -o tmp2.o -
		gcc -static -o tmp tmp.s tmp2.o
		./tmp


clean:
		rm -rf ./nsc ./src/*.o *~ ./tmp* tests/*~ tests/*.o

test:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest
		make clean


.PHONY: simpletest clean test