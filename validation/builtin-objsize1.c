#define bos(O, T)	__builtin_object_size(O, T)

struct s {
	char arr[8];
	__INT32_TYPE__ i;
	__INT32_TYPE__ padding;
};

static struct s s;

int obj_int1(void) { return bos(&s.i, 1) == 4; }
int obj_arr1(void) { return bos(&s.arr[1], 1) == 7; }

/*
 * check-name: builtin-objsize1
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-returns: 1
 */
