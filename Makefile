OS=linux

CC=gcc
CFLAGS=-O -g -Wall -Wwrite-strings
LDFLAGS=-g
AR=ar

PREFIX=$(HOME)
PROGRAMS=test-lexing test-parsing obfuscate check compile test-linearize

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h \
	  linearize.h bitmap.h ident-list.h compat.h flow.h

LIB_OBJS= target.o parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o \
	  sort.o flow.o cse.o compat-$(OS).o

LIB_FILE= sparse.a
LIBS=$(LIB_FILE)

all: $(PROGRAMS)

#
# Install the 'check' binary as 'sparse', just to confuse people.
#
#		"The better to keep you on your toes, my dear".
#
install: check bin-dir
	if test $< -nt $(PREFIX)/bin/sparse ; then install -v $< $(PREFIX)/bin/sparse ; fi

bin-dir:
	@if ! test -d $(PREFIX)/bin; then \
		echo "No '$(PREFIX)/bin' directory to install in"; \
		echo "Please create it and add it to your PATH"; \
		exit 1; \
	fi

.PHONY: bin-dir

test-lexing: test-lexing.o $(LIB_FILE)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-parsing: test-parsing.o $(LIB_FILE)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-linearize: test-linearize.o $(LIB_FILE)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-sort: test-sort.o $(LIB_FILE)
	gcc $(LDFLAGS) -o $@ $< $(LIBS)

compile: compile.o compile-i386.o $(LIB_FILE)
	$(CC) $(LDFLAGS) -o $@ $< compile-i386.o $(LIBS)

obfuscate: obfuscate.o $(LIB_FILE)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

check: check.o $(LIB_FILE)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

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
expand.o: $(LIB_H)
linearize.o: $(LIB_H)
flow.o: $(LIB_H)
sort.o: $(LIB_H)
inline.o: $(LIB_H)
target.o: $(LIB_H)
test-lexing.o: $(LIB_H)
test-parsing.o: $(LIB_H)
test-linearize.o: $(LIB_H)
compile.o: $(LIB_H) compile.h
compile-i386.o: $(LIB_H) compile.h
tokenize.o: $(LIB_H)
check.o: $(LIB_H)
obfuscate.o: $(LIB_H)

compat-linux.o: compat/strtold.c compat/id-files-stat.c compat/mmap-blob.c \
	$(LIB_H)
compat-solaris.o: compat/id-files-stat.c compat/mmap-blob.c $(LIB_H)
compat-mingw.o: $(LIB_H)
compat-cygwin.o: $(LIB_H)

pre-process.h:
	echo "#define GCC_INTERNAL_INCLUDE \"`$(CC) -print-file-name=include`\"" > pre-process.h

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS) pre-process.h
