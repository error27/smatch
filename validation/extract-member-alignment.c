/*
 * check-name: extract member alignment attributes
 * check-command: extract $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-member-alignment.c.extract
 * check-output-end
 */

typedef unsigned long long u64;
typedef u64 __attribute__((aligned(4))) compat_u64;

struct native { unsigned int a; u64 b; };
struct compat { unsigned int a; compat_u64 b; };
struct big { char c; int x __attribute__((aligned(16))); };

int layout(int cmd)
{
	switch (cmd) {
	case sizeof(struct native):
		return 1;
	case sizeof(struct compat):
		return 2;
	case sizeof(struct big):
		return 3;
	}
	return 0;
}
