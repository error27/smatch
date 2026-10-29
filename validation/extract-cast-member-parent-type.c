/*
 * check-name: extract cast member parent type
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-cast-member-parent-type.c.extract.last
 * check-output-end
 */

struct state {
	int value;
};

struct workspace {
	struct state state;
};

void target(void *ptr)
{
	((struct workspace *)ptr)->state.value = 0;
}
