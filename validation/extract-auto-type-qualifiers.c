const int c = 1;

int f(void)
{
	const __auto_type kept = 1L;
	volatile __auto_type v = 2;
	static const __auto_type s = 3;
	__auto_type x = c;

	x = 4;
	return kept + v + s + x;
}

/*
 * The declared qualifiers of an __auto_type variable are printed, and the
 * initializer's are not.
 *
 * check-name: extract auto type qualifiers
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract && grep __auto_type $file.extract
 *
 * check-output-start
extract: wrote extract-auto-type-qualifiers.c.extract
	const __auto_type kept = 0x1L;
	volatile __auto_type v = 2;
	static const __auto_type s = 3;
	__auto_type x = c;
 * check-output-end
 */
