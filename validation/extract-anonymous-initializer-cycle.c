static struct {
	void *self;
} state = {
	.self = &state,
};

int target(void)
{
	return state.self == &state;
}

/*
 * check-name: extract anonymous initializer cycle
 * check-command: extract $file && ! grep -Fq '<noident>' $file.extract && gcc -Werror -x c -fsyntax-only $file.extract
 *
 * check-output-start
extract: wrote extract-anonymous-initializer-cycle.c.extract
 * check-output-end
 */
