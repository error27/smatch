/*
 * check-name: extract typedef struct used through a pointer before its members
 * check-command: extract --last-func $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-typedef-struct-pointer-first.c.extract.last
 * check-output-end
 */

typedef struct { unsigned int minMatch; } params_t;
typedef struct { int size; } raw_t;
typedef struct ms_t ms_t;
struct ms_t {
	int x;
	const ms_t *dict;
	params_t cParams;
	const raw_t *ldm;
};

static inline unsigned int get(ms_t *ms)
{
	return ms->cParams.minMatch;
}

unsigned int last(ms_t *ms)
{
	return get(ms);
}
