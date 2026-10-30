struct state {
	int value;
};

static struct state state;
static struct state state = {
	.value = 1,
};

int target(void)
{
	return state.value;
}

/*
 * check-name: extract redeclared initializer
 * check-command: extract $file && test "$(grep -cx 'static struct state state = {' $file.extract)" = 1 && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-redeclared-initializer.c.extract
 * check-output-end
 */
