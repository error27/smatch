#include "symbol.h"
#include "target.h"
#include "machine.h"


const struct target target_arm = {
	.mach = MACH_ARM,
	.bitness = ARCH_LP32,
	.big_endian = 0,
	.unsigned_char = 1,

	.wchar = &uint_ctype,

	.bits_in_longdouble = 64,
	.max_fp_alignment = 8,
};
