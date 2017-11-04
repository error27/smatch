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
LD = gcc
AR = ar
PKG_CONFIG = pkg-config
CHECKER = ./cgcc -no-compile
CHECKER_FLAGS = -Wno-vla

DESTDIR=
PREFIX ?= $(HOME)
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/share/man
MAN1DIR=$(MANDIR)/man1

# Allow users to override build settings without dirtying their trees
# For debugging, put this in local.mk:
#
#     CFLAGS += -O0 -DDEBUG -g3 -gdwarf-2
#
-include local.mk


PROGRAMS=test-lexing test-parsing obfuscate compile graph sparse \
	 test-linearize example test-unssa test-dissect ctags
INST_PROGRAMS=sparse cgcc
INST_MAN1=sparse.1 cgcc.1

LIB_OBJS= target.o parse.o tokenize.o pre-process.o symbol.o lib.o scope.o \
	  expression.o show-parse.o evaluate.o expand.o inline.o linearize.o \
	  char.o sort.o allocate.o compat-$(OS).o ptrlist.o \
	  builtin.o \
	  stats.o \
	  flow.o cse.o simplify.o memops.o liveness.o storage.o unssa.o dissect.o

all:

GCC_BASE := $(shell $(CC) --print-file-name=)
cflags += -DGCC_BASE=\"$(GCC_BASE)\"

MULTIARCH_TRIPLET := $(shell $(CC) -print-multiarch 2>/dev/null)
cflags += -DMULTIARCH_TRIPLET=\"$(MULTIARCH_TRIPLET)\"

# Can we use GCC's generated dependencies?
HAVE_GCC_DEP:=$(shell touch .gcc-test.c && 				\
		$(CC) -c -Wp,-MD,.gcc-test.d .gcc-test.c 2>/dev/null && \
		echo 'yes'; rm -f .gcc-test.d .gcc-test.o .gcc-test.c)
ifeq ($(HAVE_GCC_DEP),yes)
cflags += -Wp,-MD,$(@D)/.$(@F).d
endif

# Can we use libxml (needed for c2xml)?
HAVE_LIBXML:=$(shell $(PKG_CONFIG) --exists libxml-2.0 2>/dev/null && echo 'yes')
ifeq ($(HAVE_LIBXML),yes)
PROGRAMS+=c2xml
INST_PROGRAMS+=c2xml
c2xml-ldlibs := $(shell $(PKG_CONFIG) --libs libxml-2.0)
c2xml-cflags := $(shell $(PKG_CONFIG) --cflags libxml-2.0)
else
$(warning Your system does not have libxml, disabling c2xml)
endif

# Can we use gtk (needed for test-inspect)
GTK_VERSION:=3.0
HAVE_GTK:=$(shell $(PKG_CONFIG) --exists gtk+-$(GTK_VERSION) 2>/dev/null && echo 'yes')
ifneq ($(HAVE_GTK),yes)
GTK_VERSION:=2.0
HAVE_GTK:=$(shell $(PKG_CONFIG) --exists gtk+-$(GTK_VERSION) 2>/dev/null && echo 'yes')
endif
ifeq ($(HAVE_GTK),yes)
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-$(GTK_VERSION))
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-$(GTK_VERSION))
PROGRAMS += test-inspect
INST_PROGRAMS += test-inspect
test-inspect-objs := test-inspect.o
test-inspect-objs += ast-model.o ast-view.o ast-inspect.o
$(foreach p,$(test-inspect-objs:.o=),$(eval $(p)-cflags := $(GTK_CFLAGS)))
test-inspect-ldlibs := $(GTK_LIBS)
else
$(warning Your system does not have gtk3/gtk2, disabling test-inspect)
endif

# Can we use LLVM (needed for ... sparse-llvm)?
LLVM_CONFIG:=llvm-config
HAVE_LLVM:=$(shell $(LLVM_CONFIG) --version >/dev/null 2>&1 && echo 'yes')
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
sparse-llvm-cflags := $(LLVM_CFLAGS)
sparse-llvm-ldflags := $(LLVM_LDFLAGS)
sparse-llvm-ldlibs := $(LLVM_LIBS)
else
$(warning LLVM 3.0 or later required. Your system has version $(LLVM_VERSION) installed.)
endif
else
$(warning sparse-llvm disabled on $(shell uname -m))
endif
else
$(warning Your system does not have llvm, disabling sparse-llvm)
endif

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



all: $(PROGRAMS)

all-installable: $(INST_PROGRAMS)

install: all-installable
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -d $(DESTDIR)$(MAN1DIR)
	$(foreach f,$(INST_PROGRAMS),$(call INSTALL_EXEC,$f,$(BINDIR)))
	$(foreach f,$(INST_MAN1),$(call INSTALL_FILE,$f,$(MAN1DIR)))


compile-objs:= compile-i386.o

ldflags += $($(@)-ldflags) $(LDFLAGS)
ldlibs  += $($(@)-ldlibs)  $(LDLIBS)
$(foreach p,$(PROGRAMS),$(eval $(p): $($(p)-objs)))
$(PROGRAMS): % : %.o $(LIBS)
	$(QUIET_LINK)$(LD) $(ldflags) -o $@ $^ $(ldlibs)

$(LIB_FILE): $(LIB_OBJS)
	$(QUIET_AR)$(AR) rcs $@ $(LIB_OBJS)

$(SLIB_FILE): $(LIB_OBJS)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -Wl,-soname,$@ -shared -o $@ $(LIB_OBJS)

OBJS := $(LIB_OBJS) $(PROGRAMS:%=%.o) $(foreach p,$(PROGRAMS),$($(p)-objs))
DEPS := $(OBJS:%.o=.%.o.d)

-include $(DEPS)


cflags   += $($(*)-cflags) $(CPPFLAGS) $(CFLAGS)
%.o: %.c
	$(QUIET_CC)$(CC) $(cflags) -c -o $@ $<

%.sc: %.c sparse
	$(QUIET_CHECK) $(CHECKER) $(CHECKER_FLAGS) $(cflags) -c $<

selfcheck: $(OBJS:.o=.sc)


clean: clean-check
	rm -f *.[oa] .*.d *.so $(PROGRAMS) $(SLIB_FILE) pre-process.h version.h

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
