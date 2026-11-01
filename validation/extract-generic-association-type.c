typedef struct { unsigned int reg; } reg_t;
typedef struct { unsigned int reg; } mcr_reg_t;
#define OFFSET(r) _Generic((r), reg_t: (r).reg, mcr_reg_t: (r).reg)

unsigned int test(reg_t reg)
{
	return OFFSET(reg);
}

/*
 * check-name: extract _Generic association types
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-generic-association-type.c.extract.last
 * check-output-end
 */
