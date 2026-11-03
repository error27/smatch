typedef struct { unsigned char b[16]; } guid_t;
typedef guid_t efi_guid_t __attribute__((aligned(4)));
typedef struct { efi_guid_t owner; unsigned char data[]; } sig_t;
struct path { unsigned char type; efi_guid_t guid; } __attribute__((packed));

int test(sig_t *s, struct path *p, int x)
{
	/* GCC fails with a duplicate case value if a size is wrong */
	switch (x) {
	case 1:
	case sizeof(sig_t) != 16 || _Alignof(sig_t) != 4 ||
	     sizeof(struct path) != 17 || _Alignof(struct path) != 1:
		return 0;
	}
	return s->data[0] + p->type;
}

/*
 * check-name: extract keeps the alignment of a member's typedef
 * check-command: extract --func=test $file && gcc -std=gnu11 -Wall -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-member-typedef-aligned.c.extract.test
 * check-output-end
 */
