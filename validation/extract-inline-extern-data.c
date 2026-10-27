extern int state;

static inline int read_state(void)
{
	return state;
}

int main(void)
{
	return read_state();
}

/*
 * check-name: extract inline extern data
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-inline-extern-data.c.extract
 * check-output-end
 */
