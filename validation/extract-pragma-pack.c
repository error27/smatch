struct inner { char c; int i; };

#pragma pack(push, 1)
struct a { char c; int i; struct inner in; };
typedef struct { char c; int i; } b_t;
struct c { char c; struct { char d; int i; } anon; };
#pragma pack(4)
struct d { char c; long l; int x:24; int y:27; char e; };
struct f { char c; int i __attribute__((aligned(16))); } __attribute__((aligned(8)));
union u { char c; long l; };
#pragma pack(pop)

int test(struct a *a, b_t *b, struct c *c, struct d *d, struct f *f,
	 union u *u, int x)
{
#pragma pack(push, 2)
	struct l { char c; int i; } l = { 0 };
#pragma pack(pop)

	/* GCC fails with a duplicate case value if a size is wrong */
	switch (x) {
	case 1:
	case sizeof(struct a) != 13 || sizeof(b_t) != 5 ||
	     sizeof(struct c) != 6 || sizeof(struct inner) != 8 ||
	     sizeof(struct l) != 6 ||
	     sizeof(struct d) != 20 || _Alignof(struct d) != 4 ||
	     __builtin_offsetof(struct d, e) != 19 ||
	     sizeof(struct f) != 8 || _Alignof(struct f) != 8 ||
	     __builtin_offsetof(struct f, i) != 4 ||
	     sizeof(union u) != 8 || _Alignof(union u) != 4:
		return 0;
	}
	return a->i + b->i + c->anon.i + l.i + d->y + f->i + u->c;
}

/*
 * check-name: extract keeps #pragma pack
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-pragma-pack.c.extract.test
 * check-output-end
 */
