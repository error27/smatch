#define bos(O, T)	__builtin_object_size(O, T)

struct s {
	char arr[8];
	__INT32_TYPE__ i;
	__INT32_TYPE__ padding;
};

static struct s s;
static char *p = &s.arr[1];
static int  *q = &s.i;

int obj_int0(void) { return bos(&s.i, 0) == 8; }
int obj_arr0(void) { return bos(&s.arr[1], 0) == 15; }

int ptr_int(struct s *p) { return bos(&p->i, 0) == -1; }
int ptr_arr(struct s *p) { return bos(&p->arr[1], 0) == -1; }

/*
 * check-name: builtin-objsize0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
