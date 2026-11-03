struct storage { long long l; char d[120]; };
struct req { int ifc; struct storage grp __attribute__((aligned(4))); } __attribute__((packed));
struct reorder { long long a[3]; } __attribute__((aligned(128)));
struct baid { char c; struct reorder entries[] __attribute__((aligned(64))); };
struct mhi { char c; long long rbase __attribute__((packed, aligned(4))); };
struct mhi2 { char c; long long rbase __attribute__((aligned(4), packed)); };
#pragma pack(4)
struct pp { char c; long long x __attribute__((aligned(2))); };
#pragma pack()
int test(struct req *r, struct baid *b, struct mhi *m, struct mhi2 *m2, struct pp *p, int x)
{
	/* GCC fails with a duplicate case value if a size is wrong */
	switch (x) {
	case 1:
	case sizeof(struct req) != 132 || _Alignof(struct req) != 4 ||
	     sizeof(struct baid) != 128 || _Alignof(struct baid) != 128 ||
	     sizeof(struct mhi) != 12 || sizeof(struct mhi2) != 12 ||
	     sizeof(struct pp) != 12 || _Alignof(struct pp) != 4:
		return 0;
	}
	return r->ifc + b->c + m->c + m2->c + p->c;
}

/*
 * check-name: extract keeps aligned() members of packed structs
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-member-aligned-attr.c.extract.test
 * check-output-end
 */
