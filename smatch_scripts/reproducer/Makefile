# Build every extracted .c file under this directory into one program.
# Functions and data which were not extracted are filled in by stubs.c.

CC ?= gcc
CFLAGS ?= -O2 -g
EXTRACT_CFLAGS = -std=gnu11 -fms-extensions -fno-strict-aliasing \
		 -fno-builtin -fno-common -funsigned-char -fshort-wchar \
		 -fno-pie -w
LDFLAGS ?= -no-pie
PROG ?= reproducer

SRCS := $(filter-out ./stubs.c,$(shell find . -name '*.c'))
OBJS := $(SRCS:.c=.o)

$(PROG): $(OBJS) stubs.o
	$(CC) $(LDFLAGS) -o $@ $^

stubs.c: $(OBJS) gen_stubs.sh
	./gen_stubs.sh $(OBJS) > $@

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRACT_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) stubs.c stubs.o $(PROG)

.PHONY: clean
