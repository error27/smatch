#
# Turkey time!
#
OS=linux

CC=gcc
CFLAGS=-O -g -Wall -Wwrite-strings -fpic
LDFLAGS=-g
AR=ar

#
# For debugging, uncomment the next one
#
CFLAGS += -DDEBUG

#
# If building with shared libraries, you might
# want to add this
#
# LDFLAGS += -Wl,-rpath,$(BINDIR)

PREFIX=$(HOME)
BINDIR=$(PREFIX)/bin
PROGRAMS=test-lexing test-parsing obfuscate compile graph sparse test-linearize example test-unssa test-dissect

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h \
	  linearize.h bitmap.h ident-list.h compat.h flow.h allocate.h \
	  storage.h ptrlist.h

LIB_OBJS= target.o parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o \
	  sort.o allocate.o compat-$(OS).o ptrlist.o \
	  flow.o cse.o simplify.o memops.o liveness.o storage.o unssa.o dissect.o

LIB_FILE= libsparse.a
SLIB_FILE= libsparse.so

LIBS=$(LIB_FILE)

all: $(PROGRAMS) $(SLIB_FILE)

install: sparse $(SLIB_FILE) bin-dir
	if test $< -nt $(BINDIR)/sparse ; then install -v $< $(BINDIR)/sparse ; install -v $(SLIB_FILE) $(BINDIR) ; fi

bin-dir:
	@if ! test -d $(BINDIR); then \
		echo "No '$(BINDIR)' directory to install in"; \
		echo "Please create it and add it to your PATH"; \
		exit 1; \
	fi

.PHONY: bin-dir

test-lexing: test-lexing.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-parsing: test-parsing.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-linearize: test-linearize.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-sort: test-sort.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

compile: compile.o compile-i386.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< compile-i386.o $(LIBS)

obfuscate: obfuscate.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

sparse: check.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

graph: graph.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

example: example.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-unssa: test-unssa.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-dissect: test-dissect.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(SLIB_FILE): $(LIB_OBJS)
	$(CC) -shared -o $@ $(LIB_OBJS)

evaluate.o: $(LIB_H)
expression.o: $(LIB_H)
lib.o: $(LIB_H)
allocate.o: $(LIB_H)
parse.o: $(LIB_H)
pre-process.o: $(LIB_H) pre-process.h
scope.o: $(LIB_H)
show-parse.o: $(LIB_H)
symbol.o: $(LIB_H)
expand.o: $(LIB_H)
linearize.o: $(LIB_H)
flow.o: $(LIB_H)
cse.o: $(LIB_H)
simplify.o: $(LIB_H)
memops.o: $(LIB_H)
liveness.o: $(LIB_H)
sort.o: $(LIB_H)
inline.o: $(LIB_H)
target.o: $(LIB_H)
test-lexing.o: $(LIB_H)
test-parsing.o: $(LIB_H)
test-linearize.o: $(LIB_H)
test-dissect.o: $(LIB_H) dissect.h
compile.o: $(LIB_H) compile.h
compile-i386.o: $(LIB_H) compile.h
tokenize.o: $(LIB_H)
check.o: $(LIB_H)
obfuscate.o: $(LIB_H)
example.o: $(LIB_H)
storage.o: $(LIB_H) storage.h
dissect.o: $(LIB_H) dissect.h
graph.o: $(LIB_H)

compat-linux.o: compat/strtold.c compat/mmap-blob.c \
	$(LIB_H)
compat-solaris.o: compat/mmap-blob.c $(LIB_H)
compat-mingw.o: $(LIB_H)
compat-cygwin.o: $(LIB_H)

pre-process.h:
	echo "#define GCC_INTERNAL_INCLUDE \"`$(CC) -print-file-name=include`\"" > pre-process.h

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS) $(SLIB_FILE) pre-process.h

% : SCCS/s.%s
