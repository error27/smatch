struct cache { int a; } __attribute__((__aligned__(64)));
struct bvec { unsigned int a; unsigned int b; unsigned long long c; } __attribute__((packed, aligned(4)));
struct __attribute__((aligned(16))) pre { char c; };
union un { char c; short s; } __attribute__((aligned(32)));
typedef struct { short s; } __attribute__((aligned(8))) td_t;
enum pk { PA, PB } __attribute__((__packed__));
typedef enum { QA = 300 } __attribute__((__packed__)) pk16_t;

int test(struct cache *c, struct bvec *b, struct pre *p, union un *u,
	 td_t *t, enum pk *e, pk16_t *f, int x)
{
	/* GCC fails with a duplicate case value if a size is wrong */
	switch (x) {
	case 1:
	case sizeof(struct cache) != 64 || _Alignof(struct cache) != 64 ||
	     sizeof(struct bvec) != 16 || _Alignof(struct bvec) != 4 ||
	     sizeof(struct pre) != 16 || _Alignof(struct pre) != 16 ||
	     sizeof(union un) != 32 || _Alignof(union un) != 32 ||
	     sizeof(td_t) != 8 || _Alignof(td_t) != 8 ||
	     sizeof(enum pk) != 1 || sizeof(pk16_t) != 2:
		return 0;
	}
	return c->a + b->a + p->c + u->c + t->s + *e + *f;
}

/*
 * check-name: extract keeps aligned and packed tag attributes
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-tag-attributes.c.extract.test
 * check-output-end
 */
