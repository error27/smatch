#include "symbol.h"
#include "target.h"
#include "machine.h"


static void init_arm64(const struct target *self)
{
	if (arch_cmodel == CMODEL_UNKNOWN)
		arch_cmodel = CMODEL_SMALL;
}

static void predefine_arm64(const struct target *self)
{
	predefine("__aarch64__", 1, "1");
}

const struct target target_arm64 = {
	.mach = MACH_ARM64,
	.bitness = ARCH_LP64,

	.big_endian = 0,
	.unsigned_char = 1,

	.wchar = &uint_ctype,

	.init = init_arm64,
	.predefine = predefine_arm64,
};
