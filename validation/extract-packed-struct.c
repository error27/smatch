struct args {
	char c;
	unsigned long long x;
};

struct args_packed {
	char c;
	unsigned long long x;
} __attribute__((__packed__));

typedef struct {
	char c;
	int x;
} __attribute__((__packed__)) packed_t;

int test(int cmd)
{
	switch (cmd) {
	case sizeof(struct args):
		return 1;
	case sizeof(struct args_packed):
		return 2;
	case sizeof(packed_t) + 100:
		return 3;
	case sizeof(int) * 2 + 100:
		return 4;
	}
	return 0;
}

/*
 * check-name: extract a packed struct
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-packed-struct.c.extract
 * check-output-end
 */
