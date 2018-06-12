// SPDX-License-Identifier: MIT
// Copyright (C) 2018 Luc Van Oostenryck

#include "utils.h"
#include "allocate.h"
#include <string.h>


void *xmemdup(const void *src, size_t len)
{
	return memcpy(__alloc_bytes(len), src, len);
}

char *xstrdup(const char *src)
{
	return xmemdup(src, strlen(src) + 1);
}
