#ifndef TARGET_H
#define TARGET_H

/*
 * Integer data types
 */
#define BITS_IN_CHAR		8
#define BITS_IN_SHORT		16
#define BITS_IN_INT		32
#define BITS_IN_LONG		32
#define BITS_IN_LONGLONG	64

#define MAX_INT_ALIGNMENT	4

/*
 * Floating point data types
 */
#define BITS_IN_FLOAT		32
#define BITS_IN_DOUBLE		64
#define BITS_IN_LONGDOUBLE	80

#define MAX_FP_ALIGNMENT	8

/*
 * Pointer data type
 */
#define BITS_IN_POINTER		32
#define POINTER_ALIGNMENT	4

/*
 * Enum data types
 */
#define BITS_IN_ENUM		32
#define ENUM_ALIGNMENT		4

#endif
