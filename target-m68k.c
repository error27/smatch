#include "symbol.h"
#include "target.h"
#include "machine.h"


static void predefine_m68k(const struct target *self)
{
	predefine("__m68k__", 1, "1");
}

const struct target target_m68k = {
	.mach = MACH_M68K,
	.bitness = ARCH_LP32,
	.big_endian = 1,
	.unsigned_char = 0,

	.wchar = &long_ctype,

	.bits_in_longdouble = 96,
	.max_fp_alignment = 4,

	.predefine = predefine_m68k,
};
