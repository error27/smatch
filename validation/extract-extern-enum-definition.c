/*
 * check-name: extract external enum definition
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-extern-enum-definition.c.extract.last
 * check-output-end
 */

enum state {
	STATE_READY,
};

extern enum state current_state;

int target(void)
{
	return current_state == STATE_READY;
}
