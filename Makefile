CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)

nsc: $(OBJS)
		gcc -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): ./src/nsc.h

nsc-stage2: nsc $(SRCS) nsc.h self.sh
		./self.sh

simpletest: nsc tests/extern.o
		./nsc tests/tests.c > tmp.s
		gcc -static -o tmp tmp.s tests/extern.o
		./tmp

test-stage2: nsc-stage2 tests/extern.o
		./nsc-stage2 tests/tests.c > tmp.s
		gcc -static -o tmp tmp.s tests/extern.o
		./tmp


clean:
		rm -rf ./nsc ./nsc-stage* ./src/*.o *~ ./tmp* tests/*~ tests/*.o

test:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest
		make clean


.PHONY: simpletest clean test