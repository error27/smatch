/*
 * Sane compat.c for Linux
 */
#define _GNU_SOURCE

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lib.h"
#include "token.h"

/*
 * Allow old BSD naming too, it would be a pity to have to make a
 * separate file just for this.
 */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/*
 * Our blob allocator enforces the strict CHUNK size
 * requirement, as a portability check.
 */
void *blob_alloc(unsigned long size)
{
	void *ptr;

	if (size & ~CHUNK)
		die("internal error: bad allocation size (%d bytes)", size);
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED)
		ptr = NULL;
	return ptr;
}

void blob_free(void *addr, unsigned long size)
{
	if (!size || (size & ~CHUNK) || ((unsigned long) addr & 512))
		die("internal error: bad blob free (%d bytes at %p)", size, addr);
	munmap(addr, size);
}

long double string_to_ld(const char *nptr, char **endptr)
{
	return strtold(nptr, endptr);
}

int identical_files(struct stream* s, struct stat *st, const char * name)
{
	return s->dev == st->st_dev && s->ino == st->st_ino;
}
