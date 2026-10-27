/*
 * check-name: extract aggregate used by member access
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-member-access-type.c.extract
 * check-output-end
 */

struct member_access_type {
	int member;
};

int consume_member(const int *member);

int use_member(struct member_access_type *value)
{
	return consume_member(&value->member);
}
