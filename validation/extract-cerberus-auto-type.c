struct item {
	unsigned long value;
};

const volatile int c = 1;
int a[3];
const int ca[3];
int f(void);
extern __seg_gs unsigned long pc;
const char *const name = "x";
struct { int x; } anon;

int g(const struct item *items, int index)
{
	__auto_type x = c;
	__auto_type p = a;
	__auto_type cp = ca;
	__auto_type fp = f;
	__auto_type v = pc;
	__auto_type n = name;
	__auto_type value = items[index].value & ~0xfUL;
	__auto_type item = &items[index];
	const __auto_type kept = 1L;
	__auto_type same = anon;

#ifdef GCC_TYPES
	_Static_assert(__builtin_types_compatible_p(__typeof__(x), int), "x");
	_Static_assert(__builtin_types_compatible_p(__typeof__(p), int *), "p");
	_Static_assert(__builtin_types_compatible_p(__typeof__(cp), const int *), "cp");
	_Static_assert(__builtin_types_compatible_p(__typeof__(fp), int (*)(void)), "fp");
	_Static_assert(__builtin_types_compatible_p(__typeof__(v), unsigned long), "v");
	_Static_assert(__builtin_types_compatible_p(__typeof__(n), const char *), "n");
	_Static_assert(__builtin_types_compatible_p(__typeof__(value), unsigned long), "value");
	_Static_assert(__builtin_types_compatible_p(__typeof__(item), const struct item *), "item");
	_Static_assert(__builtin_types_compatible_p(__typeof__(kept), const long), "kept");
#endif

	x = 2;
	return x + *p + *cp + fp() + (int)v + *n + (int)value + (int)item->value +
	       (int)kept + same.x;
}

/*
 * --cerberus prints __auto_type as the type GCC gives the variable, except
 * for the anonymous struct, which cannot be spelled.  The static asserts
 * check that GCC agrees about the types.  Sparse does not: it keeps the
 * initializer's qualifiers and address space, and does not turn arrays and
 * functions into pointers.
 *
 * check-name: extract cerberus auto type
 * check-command: extract --cerberus $file && gcc -DGCC_TYPES -Werror -fsyntax-only $file && gcc -Werror -x c -fsyntax-only $file.extract && grep -c __auto_type $file.extract
 *
 * check-output-start
extract: wrote extract-cerberus-auto-type.c.extract
1
 * check-output-end
 */
