CFLAGS=-g -Wall

PROGRAMS=test-lexing test-parsing

all: $(PROGRAMS)

test-lexing: test-lexing.o tokenize.o pre-process.o lib.o
	gcc -o $@ test-lexing.o tokenize.o pre-process.o lib.o

test-parsing: test-parsing.o parse.o tokenize.o symbol.o pre-process.o lib.o 
	gcc -o $@ test-parsing.o parse.o tokenize.o symbol.o pre-process.o lib.o

test-parsing.o: token.h parse.h
test-lexing.o: token.h
tokenize.o: token.h
parse.o: token.h parse.h
symbol.o: symbol.h token.h parse.h

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS)
