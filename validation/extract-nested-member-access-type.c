/*
 * check-name: extract aggregate used by nested member access
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-nested-member-access-type.c.extract.last
 * check-output-end
 */

struct nested_member_access_type {
	int member;
};

struct member_access_holder {
	const struct nested_member_access_type *nested;
};

int read_nested_member(struct member_access_holder *holder)
{
	return holder->nested->member;
}
