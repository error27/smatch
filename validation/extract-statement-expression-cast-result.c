/*
 * check-name: extract statement expression cast result
 * check-command: extract --last-func $file && gcc -std=gnu11 -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-statement-expression-cast-result.c.extract.last
 * check-output-end
 */

struct queue { int limits; };
struct disk { struct queue *queue; };
struct bdev { struct disk *bd_disk; int dev; };

static inline int *dev_to_limits(int *dev)
{
	return &({ (struct bdev *)((void *)dev - __builtin_offsetof(struct bdev, dev)); })->bd_disk->queue->limits;
}

int show(int *dev)
{
	return *dev_to_limits(dev);
}
