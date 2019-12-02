#include "symbol.h"
#include "target.h"
#include "machine.h"


static void init_riscv(const struct target *self)
{
	if (arch_cmodel == CMODEL_UNKNOWN)
		arch_cmodel = CMODEL_MEDLOW;
	if (fpic)
		arch_cmodel = CMODEL_PIC;
}

static void predefine_riscv(const struct target *self)
{
	predefine("__riscv", 1, "1");
	predefine("__riscv_xlen", 1, "%d", ptr_ctype.bit_size);
}

const struct target target_riscv32 = {
	.mach = MACH_RISCV32,
	.bitness = ARCH_LP32,
	.big_endian = 0,
	.unsigned_char = 1,

	.target_64bit = &target_riscv64,

	.init = init_riscv,
	.predefine = predefine_riscv,
};

const struct target target_riscv64 = {
	.mach = MACH_RISCV64,
	.bitness = ARCH_LP64,
	.big_endian = 0,
	.unsigned_char = 1,

	.target_32bit = &target_riscv32,

	.init = init_riscv,
	.predefine = predefine_riscv,
};
