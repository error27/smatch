#ifndef TARGET_H
#define TARGET_H

extern struct symbol *size_t_ctype;
extern struct symbol *ssize_t_ctype;

/*
 * For "__attribute__((aligned))"
 */
extern int max_alignment;

/*
 * Integer data types
 */
extern int bits_in_bool;
extern int bits_in_char;
extern int bits_in_short;
extern int bits_in_int;
extern int bits_in_long;
extern int bits_in_longlong;

extern int max_int_alignment;

/*
 * Floating point data types
 */
extern int bits_in_float;
extern int bits_in_double;
extern int bits_in_longdouble;

extern int max_fp_alignment;

/*
 * Pointer data type
 */
extern int bits_in_pointer;
extern int pointer_alignment;

/*
 * Enum data types
 */
extern int bits_in_enum;
extern int enum_alignment;

#endif
