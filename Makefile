CC=gcc
CFLAGS=-g -Wall
LDFLAGS=-g
AR=ar

PREFIX=$(HOME)
PROGRAMS=test-lexing test-parsing obfuscate check compile test-linearize

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h linearize.h

LIB_OBJS= parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o

LIB_FILE= sparse.a
LIBS=$(LIB_FILE)

all: $(PROGRAMS)

#
# Install the 'check' binary as 'sparse', just to confuse people.
#
#		"The better to keep you on your toes, my dear".
#
install: check
	install -C check $(PREFIX)/bin/sparse

test-lexing: test-lexing.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< $(LIBS)

test-parsing: test-parsing.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< $(LIBS)

test-linearize: test-linearize.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< $(LIBS)

compile: compile.o compile-i386.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< compile-i386.o $(LIBS)

obfuscate: obfuscate.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< $(LIBS)

check: check.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< $(LIBS)

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
linearize.o: $(LIB_H)
test-lexing.o: $(LIB_H)
test-parsing.o: $(LIB_H)
test-linearize.o: $(LIB_H)
compile.o: $(LIB_H)
compile-i386.o: $(LIB_H)
tokenize.o: $(LIB_H)

pre-process.h:
	echo "#define GCC_INTERNAL_INCLUDE \"`$(CC) -print-file-name=include`\"" > pre-process.h

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS) pre-process.h
