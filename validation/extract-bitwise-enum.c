/*
 * check-name: extract enum with bitwise enumerators
 * check-command: extract --last-func $file && grep -Fqx 'enum req_op {' $file.extract.last && gcc -std=gnu11 -Werror -x c -fsyntax-only -fms-extensions $file.extract.last
 *
 * check-output-start
extract: wrote extract-bitwise-enum.c.extract.last
 * check-output-end
 */

typedef unsigned int __attribute__((bitwise)) blk_opf_t;

enum req_op {
	REQ_OP_READ = (__attribute__((force)) blk_opf_t)0,
	REQ_OP_WRITE = (__attribute__((force)) blk_opf_t)1,
};

struct request {
	blk_opf_t cmd_flags;
};

static inline enum req_op req_op(const struct request *req)
{
	return req->cmd_flags & (__attribute__((force)) blk_opf_t)0xff;
}

int is_write(struct request *rq)
{
	enum req_op op = req_op(rq);

	return op == REQ_OP_WRITE;
}
