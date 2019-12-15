#include "symbol.h"
#include "target.h"
#include "machine.h"


const struct target target_mips32 = {
	.mach = MACH_MIPS32,
	.bitness = ARCH_LP32,
	.big_endian = 1,
	.unsigned_char = 0,

	.bits_in_longdouble = 64,
	.max_fp_alignment = 8,

	.target_64bit = &target_mips64,
};

const struct target target_mips64 = {
	.mach = MACH_MIPS64,
	.bitness = ARCH_LP64,
	.big_endian = 1,
	.unsigned_char = 0,

	.target_32bit = &target_mips32,
};
