CC=gcc
CFLAGS=-g -Wall

PROGRAMS=test-lexing test-parsing
HEADERS=token.h parse.h lib.h symbol.h scope.h
COMMON=parse.o tokenize.o pre-process.o symbol.o lib.o scope.o

all: $(PROGRAMS)

test-lexing: test-lexing.o $(COMMON)
	gcc -o $@ $< $(COMMON)

test-parsing: test-parsing.o $(COMMON)
	gcc -o $@ $< $(COMMON)

test-parsing.o: $(HEADERS)
test-lexing.o: $(HEADERS)
tokenize.o: $(HEADERS)
parse.o: $(HEADERS)
symbol.o: $(HEADERS)

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS)
