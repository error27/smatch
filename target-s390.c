#include "symbol.h"
#include "target.h"
#include "machine.h"


const struct target target_s390 = {
	.mach = MACH_S390,
	.bitness = ARCH_LP32,
	.big_endian = 1,
	.unsigned_char = 1,

	.bits_in_longdouble = 64,
	.max_fp_alignment = 8,

	.target_64bit = &target_s390x,
};

const struct target target_s390x = {
	.mach = MACH_S390X,
	.bitness = ARCH_LP64,
	.big_endian = 1,
	.unsigned_char = 1,

	.bits_in_longdouble = 64,
	.max_fp_alignment = 8,

	.target_32bit = &target_s390,
};
