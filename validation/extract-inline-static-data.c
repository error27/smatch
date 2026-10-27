static struct {
	int value;
} state;

static inline int read_state(void)
{
	return state.value;
}

int main(void)
{
	return read_state();
}

/*
 * check-name: extract inline static data
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-inline-static-data.c.extract
 * check-output-end
 */
