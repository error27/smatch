/*
 * check-name: extract nested anonymous member
 * check-command: extract --last-func $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-nested-anonymous-member.c.extract.last
 * check-output-end
 */

struct outer {
	union {
		struct {
			int member;
		};
	};
};

static int target(struct outer *outer)
{
	return outer->member;
}
