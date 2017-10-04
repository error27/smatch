VERSION=0.5.1

# Generating file version.h if current version has changed
SPARSE_VERSION:=$(shell git describe 2>/dev/null || echo '$(VERSION)')
VERSION_H := $(shell cat version.h 2>/dev/null)
ifneq ($(lastword $(VERSION_H)),"$(SPARSE_VERSION)")
$(info $(shell echo '     GEN      'version.h))
$(shell echo '#define SPARSE_VERSION "$(SPARSE_VERSION)"' > version.h)
endif

OS = linux


CC = gcc
CFLAGS = -O2 -finline-functions -fno-strict-aliasing -g
CFLAGS += -Wall -Wwrite-strings
LDFLAGS += -g
LD = gcc
AR = ar
PKG_CONFIG = pkg-config
CHECKER = ./cgcc -no-compile
CHECKER_FLAGS =

HAVE_LIBXML:=$(shell $(PKG_CONFIG) --exists libxml-2.0 2>/dev/null && echo 'yes')
HAVE_GCC_DEP:=$(shell touch .gcc-test.c && 				\
		$(CC) -c -Wp,-MD,.gcc-test.d .gcc-test.c 2>/dev/null && \
		echo 'yes'; rm -f .gcc-test.d .gcc-test.o .gcc-test.c)

GTK_VERSION:=3.0
HAVE_GTK:=$(shell $(PKG_CONFIG) --exists gtk+-$(GTK_VERSION) 2>/dev/null && echo 'yes')
ifneq ($(HAVE_GTK),yes)
	GTK_VERSION:=2.0
	HAVE_GTK:=$(shell $(PKG_CONFIG) --exists gtk+-$(GTK_VERSION) 2>/dev/null && echo 'yes')
endif

LLVM_CONFIG:=llvm-config
HAVE_LLVM:=$(shell $(LLVM_CONFIG) --version >/dev/null 2>&1 && echo 'yes')

GCC_BASE := $(shell $(CC) --print-file-name=)
CFLAGS += -DGCC_BASE=\"$(GCC_BASE)\"

MULTIARCH_TRIPLET := $(shell $(CC) -print-multiarch 2>/dev/null)
CFLAGS += -DMULTIARCH_TRIPLET=\"$(MULTIARCH_TRIPLET)\"

ifeq ($(HAVE_GCC_DEP),yes)
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d
endif

DESTDIR=
PREFIX ?= $(HOME)
BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
MANDIR=$(PREFIX)/share/man
MAN1DIR=$(MANDIR)/man1
INCLUDEDIR=$(PREFIX)/include
PKGCONFIGDIR=$(LIBDIR)/pkgconfig

PROGRAMS=test-lexing test-parsing obfuscate compile graph sparse \
	 test-linearize example test-unssa test-dissect ctags
INST_PROGRAMS=sparse cgcc
INST_MAN1=sparse.1 cgcc.1

ifeq ($(HAVE_LIBXML),yes)
PROGRAMS+=c2xml
INST_PROGRAMS+=c2xml
c2xml_EXTRA_OBJS = `$(PKG_CONFIG) --libs libxml-2.0`
LIBXML_CFLAGS := $(shell $(PKG_CONFIG) --cflags libxml-2.0)
else
$(warning Your system does not have libxml, disabling c2xml)
endif

ifeq ($(HAVE_GTK),yes)
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-$(GTK_VERSION))
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-$(GTK_VERSION))
PROGRAMS += test-inspect
INST_PROGRAMS += test-inspect
test-inspect_EXTRA_DEPS := ast-model.o ast-view.o ast-inspect.o
test-inspect_OBJS := test-inspect.o $(test-inspect_EXTRA_DEPS)
$(test-inspect_OBJS) $(test-inspect_OBJS:.o=.sc): CFLAGS += $(GTK_CFLAGS)
test-inspect_EXTRA_OBJS := $(GTK_LIBS)
else
$(warning Your system does not have gtk3/gtk2, disabling test-inspect)
endif

ifeq ($(HAVE_LLVM),yes)
ifeq ($(shell uname -m | grep -q '\(i386\|x86\)' && echo ok),ok)
LLVM_VERSION:=$(shell $(LLVM_CONFIG) --version)
ifeq ($(shell expr "$(LLVM_VERSION)" : '[3-9]\.'),2)
LLVM_PROGS := sparse-llvm
$(LLVM_PROGS): LD := g++
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags | sed -e "s/-DNDEBUG//g" | sed -e "s/-pedantic//g")
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs)
LLVM_LIBS += $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null)
PROGRAMS += $(LLVM_PROGS)
INST_PROGRAMS += sparse-llvm sparsec
sparse-llvm.o: CFLAGS += $(LLVM_CFLAGS)
sparse-llvm_EXTRA_OBJS := $(LLVM_LIBS) $(LLVM_LDFLAGS)
else
$(warning LLVM 3.0 or later required. Your system has version $(LLVM_VERSION) installed.)
endif
else
$(warning sparse-llvm disabled on $(shell uname -m))
endif
else
$(warning Your system does not have llvm, disabling sparse-llvm)
endif

LIB_H=    token.h parse.h lib.h symbol.h scope.h expression.h target.h \
	  linearize.h bitmap.h ident-list.h compat.h flow.h allocate.h \
	  storage.h ptrlist.h dissect.h

LIB_OBJS= target.o parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o \
	  char.o sort.o allocate.o compat-$(OS).o ptrlist.o \
	  builtin.o \
	  stats.o \
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
QUIET_CHECK   = $(Q:@=@echo    '     CHECK    '$<;)
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
# For debugging, put this in local.mk:
#
#     CFLAGS += -O0 -DDEBUG -g3 -gdwarf-2
#
-include local.mk


all: $(PROGRAMS) sparse.pc

all-installable: $(INST_PROGRAMS) $(LIBS) $(LIB_H) sparse.pc

install: all-installable
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -d $(DESTDIR)$(LIBDIR)
	$(Q)install -d $(DESTDIR)$(MAN1DIR)
	$(Q)install -d $(DESTDIR)$(INCLUDEDIR)/sparse
	$(Q)install -d $(DESTDIR)$(PKGCONFIGDIR)
	$(foreach f,$(INST_PROGRAMS),$(call INSTALL_EXEC,$f,$(BINDIR)))
	$(foreach f,$(INST_MAN1),$(call INSTALL_FILE,$f,$(MAN1DIR)))
	$(foreach f,$(LIBS),$(call INSTALL_FILE,$f,$(LIBDIR)))
	$(foreach f,$(LIB_H),$(call INSTALL_FILE,$f,$(INCLUDEDIR)/sparse))
	$(call INSTALL_FILE,sparse.pc,$(PKGCONFIGDIR))

sparse.pc: sparse.pc.in
	$(QUIET_GEN)sed $(SED_PC_CMD) sparse.pc.in > sparse.pc


compile_EXTRA_DEPS = compile-i386.o

$(foreach p,$(PROGRAMS),$(eval $(p): $($(p)_EXTRA_DEPS) $(LIBS)))
$(PROGRAMS): % : %.o 
	$(QUIET_LINK)$(LD) $(LDFLAGS) -o $@ $^ $($@_EXTRA_OBJS)

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(AR) rcs $@ $(LIB_OBJS)

$(SLIB_FILE): $(LIB_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -Wl,-soname,$@ -shared -o $@ $(LIB_OBJS)

DEP_FILES := $(wildcard .*.o.d)

ifneq ($(DEP_FILES),)
include $(DEP_FILES)
endif

c2xml.o c2xml.sc: CFLAGS += $(LIBXML_CFLAGS)

pre-process.sc: CHECKER_FLAGS += -Wno-vla

%.o: %.c $(LIB_H)
	$(QUIET_CC)$(CC) -o $@ -c $(CFLAGS) $<

%.sc: %.c sparse
	$(QUIET_CHECK) $(CHECKER) $(CHECKER_FLAGS) -c $(CFLAGS) $<

ALL_OBJS :=  $(LIB_OBJS) $(foreach p,$(PROGRAMS),$(p).o $($(p)_EXTRA_DEPS))
selfcheck: $(ALL_OBJS:.o=.sc)


clean: clean-check
	rm -f *.[oa] .*.d *.so $(PROGRAMS) $(SLIB_FILE) pre-process.h sparse.pc version.h

dist:
	@if test "$(SPARSE_VERSION)" != "v$(VERSION)" ; then \
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
