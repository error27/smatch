VERSION=0.4.1

OS ?= linux

CC ?= gcc
CFLAGS ?= -O2 -finline-functions -fno-strict-aliasing -g
CFLAGS += -Wall -Wwrite-strings
LDFLAGS ?= -g
AR ?= ar

HAVE_LIBXML=$(shell pkg-config --exists libxml-2.0 && echo 'yes')

#
# For debugging, uncomment the next one
#
CFLAGS += -DDEBUG

DESTDIR=
PREFIX=$(HOME)
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
MANDIR=$(PREFIX)/share/man
MAN1DIR=$(MANDIR)/man1
INCLUDEDIR=$(PREFIX)/include
PKGCONFIGDIR=$(LIBDIR)/pkgconfig

PROGRAMS=test-lexing test-parsing obfuscate compile graph sparse test-linearize example \
	 test-unssa test-dissect ctags smatch
SMATCH_FILES=smatch_flow.o smatch_conditions.o smatch_slist.o smatch_states.o \
	 smatch_helper.o smatch_hooks.o smatch_function_hooks.o smatch_extra.o \
	 smatch_extra_helper.o smatch_implied.o smatch_ignore.o \
	 smatch_tracker.o smatch_files.o smatch_expression_stacks.o
SMATCH_CHECKS=$(shell ls check_*.c | sed -e 's/\.c/.o/')


INST_PROGRAMS=sparse cgcc
INST_MAN1=sparse.1 cgcc.1

ifeq ($(HAVE_LIBXML),yes)
PROGRAMS+=c2xml
INST_PROGRAMS+=c2xml
endif

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h \
	  linearize.h bitmap.h ident-list.h compat.h flow.h allocate.h \
	  storage.h ptrlist.h dissect.h

LIB_OBJS= target.o parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o \
	  sort.o allocate.o compat-$(OS).o ptrlist.o \
	  flow.o cse.o simplify.o memops.o liveness.o storage.o unssa.o dissect.o

LIB_FILE= libsparse.a
SLIB_FILE= libsparse.so

# If you add $(SLIB_FILE) to this, you also need to add -fpic to CFLAGS above.
# Doing so incurs a noticeable performance hit, and Sparse does not have a
# stable shared library interface, so this does not occur by default.  If you
# really want a shared library, you may want to build Sparse twice: once
# without -fpic to get all the Sparse tools, and again with -fpic to get the
# shared library.
LIBS=$(LIB_FILE)

#
# Pretty print
#
V	      = @
Q	      = $(V:1=)
QUIET_CC      = $(Q:@=@echo    '     CC       '$@;)
QUIET_AR      = $(Q:@=@echo    '     AR       '$@;)
QUIET_GEN     = $(Q:@=@echo    '     GEN      '$@;)
QUIET_LINK    = $(Q:@=@echo    '     LINK     '$@;)
# We rely on the -v switch of install to print 'file -> $install_dir/file'
QUIET_INST_SH = $(Q:@=echo -n  '     INSTALL  ';)
QUIET_INST    = $(Q:@=@echo -n '     INSTALL  ';)

all: $(PROGRAMS) sparse.pc

install: $(INST_PROGRAMS) $(LIBS) $(LIB_H) sparse.pc
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -d $(DESTDIR)$(LIBDIR)
	$(Q)install -d $(DESTDIR)$(MAN1DIR)
	$(Q)install -d $(DESTDIR)$(INCLUDEDIR)/sparse
	$(Q)install -d $(DESTDIR)$(PKGCONFIGDIR)
	$(Q)for f in $(INST_PROGRAMS); do \
		$(QUIET_INST_SH)install -v $$f $(DESTDIR)$(BINDIR)/$$f || exit 1; \
	done
	$(Q)for f in $(INST_MAN1); do \
		$(QUIET_INST_SH)install -m 644 -v $$f $(DESTDIR)$(MAN1DIR)/$$f || exit 1; \
	done
	$(Q)for f in $(LIBS); do \
		$(QUIET_INST_SH)install -m 644 -v $$f $(DESTDIR)$(LIBDIR)/$$f || exit 1; \
	done
	$(Q)for f in $(LIB_H); do \
		$(QUIET_INST_SH)install -m 644 -v $$f $(DESTDIR)$(INCLUDEDIR)/sparse/$$f || exit 1; \
	done
	$(QUIET_INST)install -m 644 -v sparse.pc $(DESTDIR)$(PKGCONFIGDIR)/sparse.pc

sparse.pc: sparse.pc.in
	$(QUIET_GEN)sed 's|@version@|$(VERSION)|g;s|@prefix@|$(PREFIX)|g;s|@libdir@|$(LIBDIR)|g;s|@includedir@|$(INCLUDEDIR)|g' sparse.pc.in > sparse.pc

test-lexing: test-lexing.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-parsing: test-parsing.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-linearize: test-linearize.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-sort: test-sort.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

compile: compile.o compile-i386.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< compile-i386.o $(LIBS)

obfuscate: obfuscate.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

sparse: sparse.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

graph: graph.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

example: example.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-unssa: test-unssa.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

test-dissect: test-dissect.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

ctags: ctags.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

c2xml: c2xml.o $(LIBS)
	$(QUIET_LINK)$(CC) $(LDFLAGS)  -o $@ $< $(LIBS) `pkg-config --libs libxml-2.0`

smatch: smatch.o $(SMATCH_FILES) $(SMATCH_CHECKS) $(LIBS) 
	$(CC) $(LDFLAGS) -o $@ $< $(SMATCH_FILES) $(SMATCH_CHECKS) $(LIBS) 

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(AR) rcs $@ $(LIB_OBJS)

$(SLIB_FILE): $(LIB_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -Wl,-soname,$@ -shared -o $@ $(LIB_OBJS)

evaluate.o: $(LIB_H)
expression.o: $(LIB_H)
lib.o: $(LIB_H)
allocate.o: $(LIB_H)
ptrlist.o: $(LIB_H)
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
smatch_flow.o: $(LIB_H) smatch.h smatch_expression_stacks.h smatch_extra.h
smatch_conditions.o: $(LIB_H) smatch.h
smatch_extra.o: $(LIB_H) smatch.h smatch_extra.h
smatch_extra_helper.o: $(LIB_H) smatch.h smatch_extra.h
smatch_implied.o: $(LIB_H) smatch.h smatch_slist.h smatch_extra.h
smatch_ignore.o: $(LIB_H) smatch.h
smatch_tracker.o: $(LIB_H) smatch.h
smatch_files.o: $(LIB_H) smatch.h
smatch_hooks.o: $(LIB_H) smatch.h
smatch_function_hooks.o: $(LIB_H) smatch.h
smatch_helper.o: $(LIB_H) smatch.h
smatch_slist.o: $(LIB_H) smatch.h smatch_slist.h smatch_extra.h
smatch_states.o: $(LIB_H) smatch.h smatch_slist.h
smatch_expression_stacks.o: $(LIB_H) smatch.h
smatch.o: $(LIB_H) smatch.h
$(SMATCH_CHECKS): smatch.h smatch_slist.h
test-unssa.o: $(LIB_H)
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

c2xml.o: c2xml.c $(LIB_H)
	$(QUIET_CC)$(CC) `pkg-config --cflags libxml-2.0` -o $@ -c $(CFLAGS) $<

compat-linux.o: compat/strtold.c compat/mmap-blob.c \
	$(LIB_H)
compat-solaris.o: compat/mmap-blob.c $(LIB_H)
compat-mingw.o: $(LIB_H)
compat-cygwin.o: $(LIB_H)

pre-process.h:
	$(QUIET_GEN)echo "#define GCC_INTERNAL_INCLUDE \"`$(CC) -print-file-name=include`\"" > pre-process.h

.c.o:
	$(QUIET_CC)$(CC) -o $@ -c $(CFLAGS) $<

clean: clean-check
	rm -f *.[oa] $(PROGRAMS) $(SLIB_FILE) pre-process.h sparse.pc

dist:
	@if test "`git describe`" != "$(VERSION)" ; then \
		echo 'Update VERSION in the Makefile before running "make dist".' ; \
		exit 1 ; \
	fi
	git archive --format=tar --prefix=sparse-$(VERSION)/ HEAD^{tree} | gzip -9 > sparse-$(VERSION).tar.gz

check: all
	$(Q)cd validation && ./test-suite

clean-check:
	find validation/ \( -name "*.c.output.expected" \
	                 -o -name "*.c.output.got" \
	                 -o -name "*.c.output.diff" \
	                 -o -name "*.c.error.expected" \
	                 -o -name "*.c.error.got" \
	                 -o -name "*.c.error.diff" \
	                 \) -exec rm {} \;
