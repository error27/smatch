CC=gcc
CFLAGS=-g -Wall
AR=ar

PROGRAMS=test-lexing test-parsing obfuscate check

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h

LIB_OBJS= parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o inline.o

LIB_FILE= sparse.a

all: $(PROGRAMS)

test-lexing: test-lexing.o $(LIB_FILE)
	gcc -o $@ $< $(LIB_FILE)

test-parsing: test-parsing.o $(LIB_FILE)
	gcc -o $@ $< $(LIB_FILE)

obfuscate: obfuscate.o $(LIB_FILE)
	gcc -o $@ $< $(LIB_FILE)

check: check.o $(LIB_FILE)
	gcc -o $@ $< $(LIB_FILE)

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $(LIB_FILE) $(LIB_OBJS)

evaluate.o: $(LIB_H)
expression.o: $(LIB_H)
lib.o: $(LIB_H)
parse.o: $(LIB_H)
pre-process.o: $(LIB_H) pre-process.h
scope.o: $(LIB_H)
show-parse.o: $(LIB_H)
symbol.o: $(LIB_H)
test-lexing.o: $(LIB_H)
test-parsing.o: $(LIB_H)
tokenize.o: $(LIB_H)

pre-process.h:
	echo "#define GCC_INTERNAL_INCLUDE \"`gcc -print-file-name=include`\"" > pre-process.h

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS) pre-process.h
