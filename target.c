#ifndef TARGET_H
#define TARGET_H

#include <stdio.h>

#include "symbol.h"
#include "target.h"

struct symbol *size_t_ctype = &ulong_ctype;
struct symbol *ssize_t_ctype = &long_ctype;

/*
 * For "__attribute__((aligned))"
 */
int MAX_ALIGNMENT = 16;

/*
 * Integer data types
 */
int BITS_IN_CHAR = 8;
int BITS_IN_SHORT = 16;
int BITS_IN_INT = 32;
int BITS_IN_LONG = 32;
int BITS_IN_LONGLONG = 64;

int MAX_INT_ALIGNMENT = 4;

/*
 * Floating point data types
 */
int BITS_IN_FLOAT = 32;
int BITS_IN_DOUBLE = 64;
int BITS_IN_LONGDOUBLE = 80;

int MAX_FP_ALIGNMENT = 8;

/*
 * Pointer data type
 */
int BITS_IN_POINTER = 32;
int POINTER_ALIGNMENT = 4;

/*
 * Enum data types
 */
int BITS_IN_ENUM = 32;
int ENUM_ALIGNMENT = 4;

#endif
