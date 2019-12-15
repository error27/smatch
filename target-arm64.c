#include "symbol.h"
#include "target.h"
#include "machine.h"


const struct target target_arm64 = {
	.mach = MACH_ARM64,
	.bitness = ARCH_LP64,

	.big_endian = 0,
	.unsigned_char = 1,

	.wchar = &uint_ctype,
};
