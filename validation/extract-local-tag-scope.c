enum state {
	STATE_NONE,
};

int target(void)
{
	static const struct state {
		int value;
	} table[] = {
		{ 1 },
	};

	return table[0].value;
}

/*
 * check-name: extract local tag scope
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-local-tag-scope.c.extract
 * check-output-end
 */
