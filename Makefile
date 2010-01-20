VERSION=0.4.2

OS = linux

CC = gcc
CFLAGS = -O2 -finline-functions -fno-strict-aliasing -g
CFLAGS += -Wall -Wwrite-strings
LDFLAGS += -g
AR = ar

#
# For debugging, put this in local.mk:
#
#     CFLAGS += -O0 -DDEBUG -g3 -gdwarf-2
#

HAVE_LIBXML=$(shell pkg-config --exists libxml-2.0 2>/dev/null && echo 'yes')
HAVE_GCC_DEP=$(shell touch .gcc-test.c && 				\
		$(CC) -c -Wp,-MD,.gcc-test.d .gcc-test.c 2>/dev/null && \
		echo 'yes'; rm -f .gcc-test.d .gcc-test.o .gcc-test.c)

CFLAGS += -DGCC_BASE=\"$(shell $(CC) --print-file-name=)\"

ifeq ($(HAVE_GCC_DEP),yes)
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d
endif

DESTDIR=
PREFIX=$(HOME)
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
MANDIR=$(PREFIX)/share/man
MAN1DIR=$(MANDIR)/man1
INCLUDEDIR=$(PREFIX)/include
PKGCONFIGDIR=$(LIBDIR)/pkgconfig
SMATCHDATADIR=$(PREFIX)/share/smatch/

SMATCH_FILES=smatch_flow.o smatch_conditions.o smatch_slist.o smatch_states.o \
	 smatch_helper.o smatch_type.o smatch_hooks.o smatch_function_hooks.o \
	 smatch_modification_hooks.o smatch_extra.o \
	 smatch_ranges.o smatch_implied.o smatch_ignore.o \
	 smatch_tracker.o smatch_files.o smatch_expression_stacks.o smatch_oom.o \
	 smatch_containers.o
SMATCH_CHECKS=$(shell ls check_*.c | sed -e 's/\.c/.o/')
SMATCH_DATA=smatch_data/kernel.allocation_funcs smatch_data/kernel.balanced_funcs \
	smatch_data/kernel.frees_argument smatch_data/kernel.puts_argument \
	smatch_data/kernel.dev_queue_xmit smatch_data/kernel.returns_err_ptr \
	smatch_data/kernel.dma_funcs smatch_data/kernel.returns_held_funcs

PROGRAMS=test-lexing test-parsing obfuscate compile graph sparse \
	 test-linearize example test-unssa test-dissect ctags
INST_PROGRAMS=sparse cgcc smatch

INST_MAN1=sparse.1 cgcc.1

ifeq ($(HAVE_LIBXML),yes)
PROGRAMS+=c2xml
INST_PROGRAMS+=c2xml
c2xml_EXTRA_OBJS = `pkg-config --libs libxml-2.0`
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

define INSTALL_EXEC
	$(QUIET_INST)install -v $1 $(DESTDIR)$2/$1 || exit 1;

endef

define INSTALL_FILE
	$(QUIET_INST)install -v -m 644 $1 $(DESTDIR)$2/$1 || exit 1;

endef

SED_PC_CMD = 's|@version@|$(VERSION)|g;		\
	      s|@prefix@|$(PREFIX)|g;		\
	      s|@libdir@|$(LIBDIR)|g;		\
	      s|@includedir@|$(INCLUDEDIR)|g'



# Allow users to override build settings without dirtying their trees
-include local.mk


all: $(PROGRAMS) sparse.pc smatch

install: $(INST_PROGRAMS) $(LIBS) $(LIB_H) sparse.pc
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -d $(DESTDIR)$(LIBDIR)
	$(Q)install -d $(DESTDIR)$(MAN1DIR)
	$(Q)install -d $(DESTDIR)$(INCLUDEDIR)/sparse
	$(Q)install -d $(DESTDIR)$(PKGCONFIGDIR)
	$(Q)install -d $(DESTDIR)$(SMATCHDATADIR)/smatch_data/
	$(foreach f,$(INST_PROGRAMS),$(call INSTALL_EXEC,$f,$(BINDIR)))
	$(foreach f,$(INST_MAN1),$(call INSTALL_FILE,$f,$(MAN1DIR)))
	$(foreach f,$(LIBS),$(call INSTALL_FILE,$f,$(LIBDIR)))
	$(foreach f,$(LIB_H),$(call INSTALL_FILE,$f,$(INCLUDEDIR)/sparse))
	$(call INSTALL_FILE,sparse.pc,$(PKGCONFIGDIR))
	$(foreach f,$(SMATCH_DATA),$(call INSTALL_FILE,$f,$(SMATCHDATADIR)))

sparse.pc: sparse.pc.in
	$(QUIET_GEN)sed $(SED_PC_CMD) sparse.pc.in > sparse.pc


compile_EXTRA_DEPS = compile-i386.o

$(foreach p,$(PROGRAMS),$(eval $(p): $($(p)_EXTRA_DEPS) $(LIBS)))
$(PROGRAMS): % : %.o 
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $@ $^ $($@_EXTRA_OBJS) 

smatch: smatch.o $(SMATCH_FILES) $(SMATCH_CHECKS) $(LIBS) 
	$(CC) $(LDFLAGS) -o $@ $< $(SMATCH_FILES) $(SMATCH_CHECKS) $(LIBS) 

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(AR) rcs $@ $(LIB_OBJS)

$(SLIB_FILE): $(LIB_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -Wl,-soname,$@ -shared -o $@ $(LIB_OBJS)

smatch_flow.o: $(LIB_H) smatch.h smatch_expression_stacks.h smatch_extra.h
smatch_conditions.o: $(LIB_H) smatch.h smatch_slist.h
smatch_extra.o: $(LIB_H) smatch.h smatch_extra.h
smatch_ranges.o: $(LIB_H) smatch.h smatch_extra.h
smatch_implied.o: $(LIB_H) smatch.h smatch_slist.h smatch_extra.h
smatch_ignore.o: $(LIB_H) smatch.h
smatch_tracker.o: $(LIB_H) smatch.h
smatch_files.o: $(LIB_H) smatch.h
smatch_hooks.o: $(LIB_H) smatch.h
smatch_function_hooks.o: $(LIB_H) smatch.h smatch_slist.h smatch_extra.h
smatch_modification_hooks.o: $(LIB_H) smatch.h
smatch_containers.o: $(LIB_H) smatch.h
smatch_helper.o: $(LIB_H) smatch.h
smatch_type.o: $(LIB_H) smatch.h
smatch_slist.o: $(LIB_H) smatch.h smatch_slist.h
smatch_states.o: $(LIB_H) smatch.h smatch_slist.h smatch_extra.h
smatch_expression_stacks.o: $(LIB_H) smatch.h
smatch_oom.c: $(LIB_H) smatch.h
smatch_redefine.c: $(LIB_H) smatch.h
smatch.o: smatch.c $(LIB_H) smatch.h check_list.h
	$(CC) -c smatch.c -DSMATCHDATADIR='"$(SMATCHDATADIR)"'
$(SMATCH_CHECKS): smatch.h smatch_slist.h smatch_extra.h
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
DEP_FILES := $(wildcard .*.o.d)
$(if $(DEP_FILES),$(eval include $(DEP_FILES)))

c2xml.o: c2xml.c $(LIB_H)
	$(QUIET_CC)$(CC) `pkg-config --cflags libxml-2.0` -o $@ -c $(CFLAGS) $<

compat-linux.o: compat/strtold.c compat/mmap-blob.c $(LIB_H)
compat-solaris.o: compat/mmap-blob.c $(LIB_H)
compat-mingw.o: $(LIB_H)
compat-cygwin.o: $(LIB_H)

.c.o:
	$(QUIET_CC)$(CC) -o $@ -c $(CFLAGS) $<

clean: clean-check
	rm -f *.[oa] .*.d *.so $(PROGRAMS) $(SLIB_FILE) pre-process.h sparse.pc

dist:
	@if test "`git describe`" != "v$(VERSION)" ; then \
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
