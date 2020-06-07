CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)

nsc: $(OBJS)
		gcc -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): ./src/nsc.h

simpletest: nsc
		./test.sh

clean:
		rm -f ./nsc ./src/*.o *~ ./tmp*

test:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest
		make clean


.PHONY: simpletest clean test