CFLAGS=-std=c11 -g -fno-common
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)

nsc: $(OBJS)
		gcc -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): ./src/nsc.h

nsc-stage2: nsc $(SRCS) ./src/nsc.h self.sh
		./self.sh tmp-stage2 ./nsc nsc-stage2

nsc-stage3: nsc-stage2
		./self.sh tmp-stage3 ./nsc-stage2 nsc-stage3

simpletest: nsc tests/extern.o
		(cd tests; ../nsc -I. -DANSWER=42 tests.c) > tmp.s
		gcc -o tmp tmp.s tests/extern.o
		./tmp

test-nopic: nsc tests/extern.o
		(cd tests; ../nsc -fno-pic -I. -DANSWER=42 tests.c) > tmp.s
		gcc -static -o tmp tmp.s tests/extern.o
		./tmp

test-stage2: nsc-stage2 tests/extern.o
		(cd tests; ../nsc-stage2 -I. -DANSWER=42 tests.c) > tmp.s
		gcc -o tmp tmp.s tests/extern.o
		./tmp

test-stage3: nsc-stage3
		diff nsc-stage2 nsc-stage3

simpletest-all: simpletest test-nopic test-stage2 test-stage3

clean:
		rm -rf ./nsc* ./nsc-stage* ./src/*.o *~ ./tmp* tests/*~ tests/*.o

test:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest
		make clean

test-all:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest-all
		make clean

.PHONY: simpletest clean test