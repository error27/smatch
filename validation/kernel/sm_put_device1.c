#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sprintf.h>
#include <linux/device.h>

#include "../../check_debug.h"

struct my_data {
	int a, b, c;
	struct device dev;
};

static void dev_release(struct device *dev)
{
	struct my_data *data = container_of(dev, struct my_data, dev);
	data->a = -1;
}

int frob(struct file *file, const char __user *buf, size_t len, loff_t *ppos);
int frob(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct my_data *data;

	data = kzalloc_obj(*data);
	if (!data)
		return -ENOMEM;

	data->dev.release = dev_release;
	__smatch_implied(data->a);
	put_device(&data->dev);
	__smatch_implied(data->a);

	return 0;
}

int frob2(struct file *file, const char __user *buf, size_t len, loff_t *ppos);
int frob2(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct my_data *data;

	data = kzalloc_obj(*data);
	if (!data)
		return -ENOMEM;

	__smatch_implied(data->a);
	dev_release(&data->dev);
	__smatch_implied(data->a);

	return 0;
}

/*
 * check-name: smatch: put_device() #1
 * check-command: validation/kernel/build.sh sm_put_device1.c
 *
 * check-output-start
sm_put_device1.c:32 frob() implied: data->a = '0'
sm_put_device1.c:34 frob() implied: data->a = '(-1)'
sm_put_device1.c:48 frob2() implied: data->a = '0'
sm_put_device1.c:50 frob2() implied: data->a = '(-1)'
 * check-output-end
 */
