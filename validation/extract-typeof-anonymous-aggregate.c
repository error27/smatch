/*
 * check-name: extract typeof anonymous aggregate
 * check-command: extract $file && gcc -Werror -x c -fsyntax-only -fms-extensions $file.extract
 *
 * check-output-start
extract: wrote extract-typeof-anonymous-aggregate.c.extract
 * check-output-end
 */

struct vcpu {
	struct {
		union {
			unsigned long *type;
			const unsigned long *const_type;
			char (*rectype)[0];
		};
		unsigned long buf[16];
	} fifo[2];
};

static void initialize(struct vcpu *vcpu)
{
	unsigned int index;

	for (index = 0; index < 2; index++) {
		(void)({
			typeof(&(vcpu->fifo[index])) __tmp = &(vcpu->fifo[index]);
			typeof((&(vcpu->fifo[index])) + 1) __tmpq =
				&(vcpu->fifo[index]);
			__tmp->buf[0] = 0;
			__tmpq->buf[0] = 0;
		});
	}
}
