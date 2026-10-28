#define __bitwise __attribute__((bitwise))

typedef unsigned int __bitwise count_t;

struct element {
	int member;
};

extern struct element *elements;

int target(void)
{
	count_t count = 0;

	return (elements + (int)count)->member;
}

/*
 * check-name: extract member access through pointer arithmetic
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-member-access-binop.c.extract.last
 * check-output-end
 */
