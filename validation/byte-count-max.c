typedef unsigned long int size_t;
typedef unsigned long ulong;

extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, void *src, size_t n);
extern ulong copy_to_user(void *to, const void *from, ulong count);
extern ulong copy_from_user(void *to, const void *from, ulong count);

static void func (char *s)
{
	char d[250000];

	memset(d, 0, 250000);
	memcpy(d, s, 250000);
	copy_to_user(s, d, 250000);
	copy_from_user(d, s, 250000);
}

/*
 * check-name: byte-count-max
 *
 * check-error-start
byte-count-max.c:13:15: warning: memset with byte count of 250000
byte-count-max.c:14:15: warning: memcpy with byte count of 250000
byte-count-max.c:15:21: warning: copy_to_user with byte count of 250000
byte-count-max.c:16:23: warning: copy_from_user with byte count of 250000
 * check-error-end
 */
