#include <stdio.h>

#include "symbol.h"
#include "target.h"
#include "machine.h"

struct symbol *size_t_ctype = &uint_ctype;
struct symbol *ssize_t_ctype = &int_ctype;
struct symbol *wchar_ctype = &int_ctype;
struct symbol *wint_ctype = &uint_ctype;

/*
 * For "__attribute__((aligned))"
 */
int max_alignment = 16;

/*
 * Integer data types
 */
int bits_in_bool = 1;
int bits_in_char = 8;
int bits_in_short = 16;
int bits_in_int = 32;
int bits_in_long = 32;
int bits_in_longlong = 64;
int bits_in_longlonglong = 128;

int max_int_alignment = 4;

/*
 * Floating point data types
 */
int bits_in_float = 32;
int bits_in_double = 64;
int bits_in_longdouble = 80;

int max_fp_alignment = 8;

/*
 * Pointer data type
 */
int bits_in_pointer = 32;
int pointer_alignment = 4;

/*
 * Enum data types
 */
int bits_in_enum = 32;
int enum_alignment = 4;


void init_target(void)
{
	switch (arch_mach) {
	case MACH_X86_64:
		if (arch_m64 == ARCH_LP64)
			break;
		/* fall through */
	case MACH_I386:
	case MACH_M68K:
	case MACH_SPARC32:
	case MACH_PPC32:
		wchar_ctype = &long_ctype;
		break;
	case MACH_ARM:
	case MACH_ARM64:
		wchar_ctype = &uint_ctype;
		break;
	default:
		break;
	}

#if defined(__CYGWIN__)
	wchar_ctype = &ushort_ctype;
#endif
#if defined(__FreeBSD__) || defined(__APPLE__)
	wint_ctype = &int_ctype;
#endif
}
