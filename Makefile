VERSION=0.6.4

########################################################################
# The following variables can be overwritten from the command line
OS = linux


CC = gcc
CXX = g++
LD = $(CC)
AR = ar

CFLAGS ?= -O2 -g

DESTDIR ?=
PREFIX ?= $(HOME)
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man

PKG_CONFIG ?= pkg-config

CHECKER_FLAGS ?= -Wno-vla

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
LIB_OBJS += dominate.o
LIB_OBJS += evaluate.o
LIB_OBJS += expand.o
LIB_OBJS += expression.o
LIB_OBJS += flow.o
LIB_OBJS += flowgraph.o
LIB_OBJS += inline.o
LIB_OBJS += ir.o
LIB_OBJS += lib.o
LIB_OBJS += linearize.o
LIB_OBJS += liveness.o
LIB_OBJS += memops.o
LIB_OBJS += opcode.o
LIB_OBJS += optimize.o
LIB_OBJS += options.o
LIB_OBJS += parse.o
LIB_OBJS += predefine.o
LIB_OBJS += pre-process.o
LIB_OBJS += ptrlist.o
LIB_OBJS += ptrmap.o
LIB_OBJS += scope.o
LIB_OBJS += show-parse.o
LIB_OBJS += simplify.o
LIB_OBJS += sort.o
LIB_OBJS += ssa.o
LIB_OBJS += stats.o
LIB_OBJS += storage.o
LIB_OBJS += symbol.o
LIB_OBJS += target.o
LIB_OBJS += target-alpha.o
LIB_OBJS += target-arm.o
LIB_OBJS += target-arm64.o
LIB_OBJS += target-bfin.o
LIB_OBJS += target-default.o
LIB_OBJS += target-h8300.o
LIB_OBJS += target-m68k.o
LIB_OBJS += target-microblaze.o
LIB_OBJS += target-mips.o
LIB_OBJS += target-nds32.o
LIB_OBJS += target-nios2.o
LIB_OBJS += target-openrisc.o
LIB_OBJS += target-ppc.o
LIB_OBJS += target-riscv.o
LIB_OBJS += target-s390.o
LIB_OBJS += target-sh.o
LIB_OBJS += target-sparc.o
LIB_OBJS += target-x86.o
LIB_OBJS += target-xtensa.o
LIB_OBJS += tokenize.o
LIB_OBJS += unssa.o
LIB_OBJS += utils.o
LIB_OBJS += version.o

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
PROGRAMS += test-show-type
PROGRAMS += test-unssa

INST_PROGRAMS=sparse cgcc
INST_MAN1=sparse.1 cgcc.1


all:

########################################################################
# common flags/options/...

cflags = -fno-strict-aliasing
cflags += -Wall -Wwrite-strings

GCC_BASE := $(shell $(CC) --print-file-name=)
cflags += -DGCC_BASE=\"$(GCC_BASE)\"

MULTIARCH_TRIPLET := $(shell $(CC) -print-multiarch 2>/dev/null)
cflags += -DMULTIARCH_TRIPLET=\"$(MULTIARCH_TRIPLET)\"


bindir := $(DESTDIR)$(BINDIR)
man1dir := $(DESTDIR)$(MANDIR)/man1

########################################################################
# target specificities

compile: compile-i386.o
EXTRA_OBJS += compile-i386.o

# Can we use GCC's generated dependencies?
HAVE_GCC_DEP:=$(shell touch .gcc-test.c && 				\
		$(CC) -c -Wp,-MP,-MMD,.gcc-test.d .gcc-test.c 2>/dev/null && \
		echo 'yes'; rm -f .gcc-test.d .gcc-test.o .gcc-test.c)
ifeq ($(HAVE_GCC_DEP),yes)
cflags += -Wp,-MP,-MMD,$(@D)/.$(@F).d
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

HAVE_SQLITE := $(shell $(PKG_CONFIG) --exists sqlite3 2>/dev/null && echo 'yes')
ifeq ($(HAVE_SQLITE),yes)
SQLITE_VERSION:=$(shell $(PKG_CONFIG) --modversion sqlite3)
SQLITE_VNUMBER:=$(shell printf '%d%02d%02d' $(subst ., ,$(SQLITE_VERSION)))
ifeq ($(shell expr "$(SQLITE_VNUMBER)" '>=' 32400),1)
PROGRAMS += semind
INST_PROGRAMS += semind
INST_MAN1 += semind.1
semind-ldlibs := $(shell $(PKG_CONFIG) --libs sqlite3)
semind-cflags := $(shell $(PKG_CONFIG) --cflags sqlite3)
semind-cflags += -std=gnu99
else
$(warning Your SQLite3 version ($(SQLITE_VERSION)) is too old, 3.24.0 or later is required.)
endif
else
$(warning Your system does not have sqlite3, disabling semind)
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
ast-view-cflags := $(GTK_CFLAGS)
ast-model-cflags := $(GTK_CFLAGS)
ast-inspect-cflags := $(GTK_CFLAGS)
test-inspect-cflags := $(GTK_CFLAGS)
test-inspect-ldlibs := $(shell $(PKG_CONFIG) --libs gtk+-$(GTK_VERSION))
test-inspect: ast-model.o ast-view.o ast-inspect.o
EXTRA_OBJS += ast-model.o ast-view.o ast-inspect.o
PROGRAMS += test-inspect
INST_PROGRAMS += test-inspect
else
$(warning Your system does not have gtk3/gtk2, disabling test-inspect)
endif

# Can we use LLVM (needed for ... sparse-llvm)?
LLVM_CONFIG:=llvm-config
HAVE_LLVM:=$(shell $(LLVM_CONFIG) --version >/dev/null 2>&1 && echo 'yes')
ifeq ($(HAVE_LLVM),yes)
arch := $(shell uname -m)
ifeq (${MULTIARCH_TRIPLET},x86_64-linux-gnux32)
arch := x32
endif
ifneq ($(filter ${arch},i386 i486 i586 i686 x86_64 amd64),)
LLVM_VERSION:=$(shell $(LLVM_CONFIG) --version)
LLVM_VERSION_MAJOR:=$(firstword $(subst ., ,$(LLVM_VERSION)))
ifeq ($(shell expr "$(LLVM_VERSION_MAJOR)" '>=' 3),1)
LLVM_PROGS := sparse-llvm
$(LLVM_PROGS): LD := $(CXX)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cppflags)
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs)
LLVM_LIBS += $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null)
LLVM_LIBS += $(shell $(LLVM_CONFIG) --cxxflags | grep -F -q -e '-stdlib=libc++' && echo -lc++)
PROGRAMS += $(LLVM_PROGS)
INST_PROGRAMS += sparse-llvm sparsec
sparse-llvm-cflags := $(LLVM_CFLAGS)
sparse-llvm-ldflags := $(LLVM_LDFLAGS)
sparse-llvm-ldlibs := $(LLVM_LIBS)
else
$(warning LLVM 3.0 or later required. Your system has version $(LLVM_VERSION) installed.)
endif
else
$(warning sparse-llvm disabled on ${arch})
endif
else
$(warning Your system does not have llvm, disabling sparse-llvm)
endif

ifeq ($(HAVE_BOOLECTOR),yes)
PROGRAMS += scheck
scheck-cflags  := -I${BOOLECTORDIR}/include/boolector
scheck-ldflags := -L${BOOLECTORDIR}/lib
scheck-ldlibs  := -lboolector -llgl -lbtor2parser
endif

########################################################################
LIBS := libsparse.a
OBJS := $(LIB_OBJS) $(EXTRA_OBJS) $(PROGRAMS:%=%.o)

# Pretty print
V := @
Q := $(V:1=)

########################################################################
all: $(PROGRAMS)

ldflags += $($(@)-ldflags) $(LDFLAGS)
ldlibs  += $($(@)-ldlibs)  $(LDLIBS)
$(PROGRAMS): % : %.o $(LIBS)
	@echo "  LD      $@"
	$(Q)$(LD) $(ldflags) $^ $(ldlibs) -o $@

libsparse.a: $(LIB_OBJS)
	@echo "  AR      $@"
	$(Q)$(AR) rcs $@ $^


cflags   += $($(*)-cflags) $(CPPFLAGS) $(CFLAGS)
%.o: %.c
	@echo "  CC      $@"
	$(Q)$(CC) $(cflags) -c -o $@ $<

%.sc: %.c sparse
	@echo "  CHECK   $<"
	$(Q)CHECK=./sparse ./cgcc -no-compile $(CHECKER_FLAGS) $(cflags) -c $<

selfcheck: $(OBJS:.o=.sc)

SPARSE_VERSION:=$(shell git describe --dirty 2>/dev/null || echo '$(VERSION)')
version.o: version.h
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
validation/%: $(PROGRAMS) FORCE
	$(Q)validation/test-suite $*


clean: clean-check
	@rm -f *.[oa] .*.d $(PROGRAMS) version.h
clean-check:
	@echo "  CLEAN"
	@find validation/ \( -name "*.c.output.*" \
			  -o -name "*.c.error.*" \
			  -o -name "*.o" \
	                  \) -exec rm {} \;


install: install-bin install-man
install-bin: $(INST_PROGRAMS:%=$(bindir)/%)
install-man: $(INST_MAN1:%=$(man1dir)/%)

$(bindir)/%: %
	@echo "  INSTALL $@"
	$(Q)install -D        $< $@ || exit 1;
$(man1dir)/%: %
	@echo "  INSTALL $@"
	$(Q)install -D -m 644 $< $@ || exit 1;

.PHONY: FORCE

# GCC's dependencies
-include $(OBJS:%.o=.%.o.d)
