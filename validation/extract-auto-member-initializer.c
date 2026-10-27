/*
 * check-name: extract auto member initializer
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-auto-member-initializer.c.extract
 * check-output-end
 */

struct auto_member_initializer {
	unsigned long value;
};

unsigned long auto_member_value(const struct auto_member_initializer *items,
				int index)
{
	__auto_type value = items[index].value & ~0xfUL;

	return value;
}
