#ifndef UTILS_H
#define UTILS_H

///
// Miscellaneous utilities
// -----------------------

#include <stddef.h>

///
// duplicate a memory buffer in a newly allocated buffer.
// @src: a pointer to the memory buffer to be duplicated
// @len: the size of the memory buffer to be duplicated
// @return: a pointer to a copy of @src allocated via
//	:func:`__alloc_bytes()`.
void *xmemdup(const void *src, size_t len);

///
// duplicate a null-terminated string in a newly allocated buffer.
// @src: a pointer to string to be duplicated
// @return: a pointer to a copy of @str allocated via
//	:func:`__alloc_bytes()`.
char *xstrdup(const char *src);

#endif
