#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

struct legacy_io {
	u8 offset;
	u8 registers[];
};

int test_user_overflow(void __user *argp, const u8 *buf);
int test_user_overflow(void __user *argp, const u8 *buf)
{
	struct legacy_io *data;
	u8 size;

	if (copy_from_user(&size, argp, sizeof(size)))
		return -EFAULT;

	data = kzalloc(sizeof(*data) + size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(data->registers, buf, size);
	kfree(data);

	return 0;
}

/*
 * check-name: smatch: user overflow #1
 * check-command: validation/kernel/build.sh sm_user_overflow1.c
 *
 * check-output-start
 * check-output-end
 */
