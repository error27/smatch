CFLAGS=-g -Wall

test-lexing: test-lexing.o tokenize.o
	gcc -o $@ test-lexing.o tokenize.o

test-lexing.o: token.h
tokenize.o: token.h

clean:
	rm -f *.o
