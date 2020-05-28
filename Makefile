CFLAGS=-std=c11 -g -static

main: main.c

simpletest: main
		./test.sh

clean:
		rm -f main *.o *~ tmp*

test:
		docker run --rm -v ${HOME}/documents/ccompiler:/ccompiler -w /ccompiler compiler make simpletest
		make clean


.PHONY: simpletest clean test