CC=gcc
CFLAGS=-g -Wall

PROGRAMS=test-lexing test-parsing

HEADERS=token.h parse.h lib.h symbol.h scope.h expression.h

COMMON=	parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	expression.o show-parse.o evaluate.o

all: $(PROGRAMS)

test-lexing: test-lexing.o $(COMMON)
	gcc -o $@ $< $(COMMON)

test-parsing: test-parsing.o $(COMMON)
	gcc -o $@ $< $(COMMON)

evaluate.o: $(HEADERS)
expression.o: $(HEADERS)
lib.o: $(HEADERS)
parse.o: $(HEADERS)
pre-process.o: $(HEADERS)
scope.o: $(HEADERS)
show-parse.o: $(HEADERS)
symbol.o: $(HEADERS)
test-lexing.o: $(HEADERS)
test-parsing.o: $(HEADERS)
tokenize.o: $(HEADERS)

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS)
