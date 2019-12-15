#include "symbol.h"
#include "target.h"
#include "machine.h"


const struct target target_riscv32 = {
	.mach = MACH_RISCV32,
	.bitness = ARCH_LP32,
	.big_endian = 0,
	.unsigned_char = 1,

	.target_64bit = &target_riscv64,
};

const struct target target_riscv64 = {
	.mach = MACH_RISCV64,
	.bitness = ARCH_LP64,
	.big_endian = 0,
	.unsigned_char = 1,

	.target_32bit = &target_riscv32,
};
