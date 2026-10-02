#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

void test_user_len_buffer(void __user *argp, const void *src);

void test_user_len_buffer(void __user *argp, const void *src)
{
	unsigned int count;
	int *buf;

	if (copy_from_user(&count, argp, sizeof(count)))
		return;
	if (!count)
		return;

	buf = kmalloc_array(count, sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return;

	memcpy(buf, src, sizeof(*buf));
	memcpy(buf, src, 2 * sizeof(*buf));
	kfree(buf);
}

/*
 * check-name: smatch: user length buffer comparison
 * check-command: validation/kernel/build.sh sm_user_len_buffer.c
 *
 * check-output-start
sm_user_len_buffer.c:23 test_user_len_buffer() warn: buffer 'buf' too small user_len=4-17179869180 for 8 byte copy
 * check-output-end
 */
