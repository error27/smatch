/*	
 * MinGW Compatibility functions	
 *	
 *	
 *  Licensed under the Open Software License version 1.1	
 */	
	
	
	
#include <stdarg.h>	
#include <windef.h>	
#include <winbase.h>	
#include <stdlib.h>	
#include <string.h>	
	
#include "lib.h"
#include "allocate.h"
#include "token.h"
	
void *blob_alloc(unsigned long size)	
{	
	void *ptr;	
	ptr = malloc(size);	
	if (ptr != NULL)	
		memset(ptr, 0, size);	
	return ptr;	
}	
	
void blob_free(void *addr, unsigned long size)	
{	
	free(addr);	
}	
	
long double string_to_ld(const char *nptr, char **endptr) 	
{	
	return strtod(nptr, endptr);	
}
