/*
 * BSD Compatibility functions
 *
 *
 *  Licensed under the Open Software License version 1.1
 */

#include <sys/types.h>
#include <string.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"

#include "compat/mmap-blob.c"

long double string_to_ld(const char *nptr, char **endptr)
{
	return strtod(nptr, endptr);
}
