/*	
 * Cygwin Compatibility functions	
 *	
 *	
 *  Licensed under the Open Software License version 1.1	
 */	
	
	
	
#include <sys/mman.h>	
#include <stdlib.h>	
#include <string.h>	
#include <sys/stat.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"
	
void *blob_alloc(unsigned long size)	
{	
	void *ptr;	
	size = (size + 4095) & ~4095;	
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);	
	if (ptr == MAP_FAILED)	
		ptr = NULL;	
	else	
		memset(ptr, 0, size);	
	return ptr;	
}	
	
void blob_free(void *addr, unsigned long size)	
{	
	size = (size + 4095) & ~4095;	
	munmap(addr, size);	
}	
	
long double string_to_ld(const char *nptr, char **endptr) 	
{	
	return strtod(nptr, endptr);	
}	
	
int identical_files(struct stream* s, struct stat *st, const char * name) 	
{	
	return (s->dev == st->st_dev && s->ino == st->st_ino);	
}	
