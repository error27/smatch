int f(void);

int test(void)
{
	int ret = 0;

	ret = f();
	if (ret) {
		(void)sizeof(char[-!(__builtin_constant_p(ret) ? (ret) < 0 : 1)]);
		return ret;
	}
	return 0;
}

/*
 * check-name: extract does not use the initializer of a variable which is assigned later
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-constant-p-assigned-variable.c.extract.last
 * check-output-end
 */
