/*
 * check-name: extract aggregate used by anonymous member access
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-anonymous-member-access-type.c.extract
 * check-output-end
 */

struct anonymous_member_type {
	int value;
};

struct anonymous_member_holder {
	union {
		struct {
			struct anonymous_member_type *member;
		};
	};
};

int read_anonymous_member(struct anonymous_member_holder *holder)
{
	return holder->member->value;
}
