CFLAGS=-g -Wall

all: test-lexing test-parsing

test-lexing: test-lexing.o tokenize.o lib.o
	gcc -o $@ test-lexing.o tokenize.o lib.o

test-parsing: test-parsing.o parse.o tokenize.o lib.o
	gcc -o $@ test-parsing.o parse.o tokenize.o lib.o

test-parsing: token.h
test-lexing.o: token.h
tokenize.o: token.h
parse.o: token.h

clean:
	rm -f *.o
