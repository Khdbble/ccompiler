CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

nsc: $(OBJS)
		$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): nsc.h

simpletest: nsc
		./test.sh

clean:
		rm -f nsc *.o *~ tmp*

test:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest
		make clean


.PHONY: simpletest clean test