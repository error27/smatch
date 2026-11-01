struct firmware { int size; };
struct wpr { unsigned long long base; };
struct rm { const struct wpr *wpr; };

struct gsp {
	struct {
		const struct firmware *rm;
	} fws;
	struct rm *rm;
};

unsigned long long test(struct gsp *gsp)
{
	return gsp->rm->wpr->base;
}

/*
 * check-name: extract a member with the same name as a member of a named anonymous struct
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-member-in-named-anonymous-struct.c.extract.last
 * check-output-end
 */
