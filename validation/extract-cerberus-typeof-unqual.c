struct pair {
	int a;
};

static const int value = 1;
static volatile struct pair pair;
static const char *const name = "x";
static unsigned long __seg_gs counter;
static struct {
	int x;
} anonymous;

static int use(void)
{
	__typeof_unqual__(value) copy = value;
	__typeof_unqual__(pair) local = { 0 };
	__typeof_unqual__(name) other = name;
	__typeof_unqual__(counter) seen = counter;
	__typeof_unqual__(anonymous) same = anonymous;

	_Static_assert(__builtin_types_compatible_p(__typeof_unqual__(value), int), "value");
	_Static_assert(__builtin_types_compatible_p(__typeof_unqual__(pair), struct pair), "pair");
	_Static_assert(__builtin_types_compatible_p(__typeof_unqual__(name), const char *), "name");
	_Static_assert(__builtin_types_compatible_p(__typeof_unqual__(counter), unsigned long), "counter");

	copy = 2;
	local.a = 1;
	other = "y";
	seen = 3;
	same.x = 1;
	return copy + local.a + *other + (int)seen + same.x +
	       (__typeof_unqual__(value))1 + (int)sizeof(__typeof_unqual__(pair));
}

/*
 * --cerberus prints __typeof_unqual__() as the type it stands for, except
 * for the anonymous struct, which cannot be spelled.  The static asserts
 * check that GCC agrees about the types.
 *
 * check-name: extract cerberus typeof unqual
 * check-command: extract --cerberus $file && gcc -Werror -fsyntax-only $file && gcc -Werror -x c -fsyntax-only $file.extract && grep -c __typeof_unqual__ $file.extract
 *
 * check-output-start
extract: wrote extract-cerberus-typeof-unqual.c.extract
1
 * check-output-end
 */
