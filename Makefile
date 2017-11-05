VERSION=0.5.1

########################################################################
# The following variables can be overwritten from the command line
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
SPARSE_LOCAL_CONFIG ?= local.mk
-include ${SPARSE_LOCAL_CONFIG}
########################################################################


LIB_OBJS :=
LIB_OBJS += allocate.o
LIB_OBJS += builtin.o
LIB_OBJS += char.o
LIB_OBJS += compat-$(OS).o
LIB_OBJS += cse.o
LIB_OBJS += dissect.o
LIB_OBJS += evaluate.o
LIB_OBJS += expand.o
LIB_OBJS += expression.o
LIB_OBJS += flow.o
LIB_OBJS += inline.o
LIB_OBJS += lib.o
LIB_OBJS += linearize.o
LIB_OBJS += liveness.o
LIB_OBJS += memops.o
LIB_OBJS += parse.o
LIB_OBJS += pre-process.o
LIB_OBJS += ptrlist.o
LIB_OBJS += scope.o
LIB_OBJS += show-parse.o
LIB_OBJS += simplify.o
LIB_OBJS += sort.o
LIB_OBJS += stats.o
LIB_OBJS += storage.o
LIB_OBJS += symbol.o
LIB_OBJS += target.o
LIB_OBJS += tokenize.o
LIB_OBJS += unssa.o

PROGRAMS :=
PROGRAMS += compile
PROGRAMS += ctags
PROGRAMS += example
PROGRAMS += graph
PROGRAMS += obfuscate
PROGRAMS += sparse
PROGRAMS += test-dissect
PROGRAMS += test-lexing
PROGRAMS += test-linearize
PROGRAMS += test-parsing
PROGRAMS += test-unssa

INST_PROGRAMS=sparse cgcc
INST_MAN1=sparse.1 cgcc.1


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

LIBS := libsparse.a

#
# Pretty print
#
V	      = @
Q	      = $(V:1=)


compile_OBJS := compile-i386.o

all: $(PROGRAMS)

ldflags += $($(@)-ldflags) $(LDFLAGS)
ldlibs  += $($(@)-ldlibs)  $(LDLIBS)
$(foreach p,$(PROGRAMS),$(eval $(p): $($(p)-objs)))
$(PROGRAMS): % : %.o $(LIBS)
	@echo "  LD      $@"
	$(Q)$(LD) $(ldflags) $^ $(ldlibs) -o $@

libsparse.a: $(LIB_OBJS)
	@echo "  AR      $@"
	$(Q)$(AR) rcs $@ $^

OBJS := $(LIB_OBJS) $(PROGRAMS:%=%.o) $(foreach p,$(PROGRAMS),$($(p)-objs))
DEPS := $(OBJS:%.o=.%.o.d)

-include $(DEPS)


cflags   += $($(*)-cflags) $(CPPFLAGS) $(CFLAGS)
%.o: %.c
	@echo "  CC      $@"
	$(Q)$(CC) $(cflags) -c -o $@ $<

%.sc: %.c sparse
	@echo "  CHECK   $<"
	$(Q) $(CHECKER) $(CHECKER_FLAGS) $(cflags) -c $<

selfcheck: $(OBJS:.o=.sc)


SPARSE_VERSION:=$(shell git describe 2>/dev/null || echo '$(VERSION)')
lib.o: version.h
version.h: FORCE
	@echo '#define SPARSE_VERSION "$(SPARSE_VERSION)"' > version.h.tmp
	@if cmp -s version.h version.h.tmp; then \
		rm version.h.tmp; \
	else \
		echo "  GEN     $@"; \
		mv version.h.tmp version.h; \
	fi


check: all
	$(Q)cd validation && ./test-suite


clean: clean-check
	@rm -f *.[oa] .*.d $(PROGRAMS) version.h
clean-check:
	@echo "  CLEAN"
	@find validation/ \( -name "*.c.output.*" \
			  -o -name "*.c.error.*" \
			  -o -name "*.o" \
	                  \) -exec rm {} \;


install: $(INST_PROGRAMS) $(INST_MAN1) install-dirs install-bin install-man
install-dirs:
	$(Q)install -d $(DESTDIR)$(BINDIR)
	$(Q)install -d $(DESTDIR)$(MAN1DIR)
install-bin: $(INST_PROGRAMS:%=$(DESTDIR)$(BINDIR)/%)
install-man: $(INST_MAN1:%=$(DESTDIR)$(MAN1DIR)/%)

$(DESTDIR)$(BINDIR)/%: %
	@echo "  INSTALL $@"
	$(Q)install        $< $@ || exit 1;
$(DESTDIR)$(MAN1DIR)/%: %
	@echo "  INSTALL $@"
	$(Q)install -m 644 $< $@ || exit 1;

.PHONY: FORCE
