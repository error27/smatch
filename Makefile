#
# Turkey time!
#
VERSION=0.2

OS=linux

CC=gcc
CFLAGS=-O -g -Wall -Wwrite-strings -fpic
LDFLAGS=-g
AR=ar

#
# For debugging, uncomment the next one
#
CFLAGS += -DDEBUG

DESTDIR=
PREFIX=$(HOME)
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INCLUDEDIR=$(PREFIX)/include
PKGCONFIGDIR=$(LIBDIR)/pkgconfig

PROGRAMS=test-lexing test-parsing obfuscate compile graph sparse test-linearize example \
	 test-unssa test-dissect ctags
INST_PROGRAMS=sparse cgcc

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h \
	  linearize.h bitmap.h ident-list.h compat.h flow.h allocate.h \
	  storage.h ptrlist.h dissect.h

LIB_OBJS= target.o parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o \
	  sort.o allocate.o compat-$(OS).o ptrlist.o \
	  flow.o cse.o simplify.o memops.o liveness.o storage.o unssa.o dissect.o

LIB_FILE= libsparse.a
SLIB_FILE= libsparse.so

LIBS=$(LIB_FILE)

all: $(PROGRAMS) sparse.pc

install: $(INST_PROGRAMS) $(LIBS) $(LIB_H) sparse.pc
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)/sparse
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	for f in $(INST_PROGRAMS); do \
		install -v $$f $(DESTDIR)$(BINDIR)/$$f || exit 1; \
	done
	for f in $(LIBS); do \
		install -m 644 -v $$f $(DESTDIR)$(LIBDIR)/$$f || exit 1; \
	done
	for f in $(LIB_H); do \
		install -m 644 -v $$f $(DESTDIR)$(INCLUDEDIR)/sparse/$$f || exit 1; \
	done
	install -m 644 -v sparse.pc $(DESTDIR)$(PKGCONFIGDIR)/sparse.pc

sparse.pc: sparse.pc.in
	sed 's|@version@|$(VERSION)|g;s|@prefix@|$(PREFIX)|g;s|@libdir@|$(LIBDIR)|g;s|@includedir@|$(INCLUDEDIR)|g' sparse.pc.in > sparse.pc

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

sparse: sparse.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

graph: graph.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

example: example.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-unssa: test-unssa.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-dissect: test-dissect.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

ctags: ctags.o $(LIBS)
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
test-dissect.o: $(LIB_H)
ctags.o: $(LIB_H)
compile.o: $(LIB_H) compile.h
compile-i386.o: $(LIB_H) compile.h
tokenize.o: $(LIB_H)
sparse.o: $(LIB_H)
obfuscate.o: $(LIB_H)
example.o: $(LIB_H)
storage.o: $(LIB_H)
dissect.o: $(LIB_H)
graph.o: $(LIB_H)

compat-linux.o: compat/strtold.c compat/mmap-blob.c \
	$(LIB_H)
compat-solaris.o: compat/mmap-blob.c $(LIB_H)
compat-mingw.o: $(LIB_H)
compat-cygwin.o: $(LIB_H)

pre-process.h:
	echo "#define GCC_INTERNAL_INCLUDE \"`$(CC) -print-file-name=include`\"" > pre-process.h

clean:
	rm -f *.[oasi] core core.[0-9]* $(PROGRAMS) $(SLIB_FILE) pre-process.h sparse.pc
