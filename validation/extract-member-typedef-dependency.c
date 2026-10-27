/*
 * check-name: extract member typedef dependency
 * check-command: extract $file && grep -qx 'typedef unsigned int blk_opf_t;' $file.extract && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-member-typedef-dependency.c.extract
 * check-output-end
 */

typedef unsigned int blk_opf_t;

struct bio {
	blk_opf_t bi_opf;
};

struct bio bio;
