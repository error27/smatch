#include "symbol.h"
#include "target.h"
#include "machine.h"


const struct target target_ppc32 = {
	.mach = MACH_PPC32,
	.bitness = ARCH_LP32,
	.big_endian = 1,
	.unsigned_char = 1,

	.wchar = &long_ctype,

	.target_64bit = &target_ppc64,
};


const struct target target_ppc64 = {
	.mach = MACH_PPC64,
	.bitness = ARCH_LP64,
	.big_endian = 1,
	.unsigned_char = 1,

	.target_32bit = &target_ppc32,
};
