/*
 * check-name: extract asm goto always inline
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-asm-goto-always-inline.c.extract
 * check-output-end
 */

static inline int branch(const int value)
{
	asm goto("jmp %l[yes]" : : "i" (value) : : yes);
	return 0;
yes:
	return 1;
}

int call_branch(void)
{
	return branch(0);
}
