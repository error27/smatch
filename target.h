#ifndef TARGET_H
#define TARGET_H

extern struct symbol *size_t_ctype;
extern struct symbol *ssize_t_ctype;

/*
 * For "__attribute__((aligned))"
 */
extern int MAX_ALIGNMENT;

/*
 * Integer data types
 */
extern int BITS_IN_CHAR;
extern int BITS_IN_SHORT;
extern int BITS_IN_INT;
extern int BITS_IN_LONG;
extern int BITS_IN_LONGLONG;

extern int MAX_INT_ALIGNMENT;

/*
 * Floating point data types
 */
extern int BITS_IN_FLOAT;
extern int BITS_IN_DOUBLE;
extern int BITS_IN_LONGDOUBLE;

extern int MAX_FP_ALIGNMENT;

/*
 * Pointer data type
 */
extern int BITS_IN_POINTER;
extern int POINTER_ALIGNMENT;

/*
 * Enum data types
 */
extern int BITS_IN_ENUM;
extern int ENUM_ALIGNMENT;

#endif
