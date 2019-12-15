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
	static const char *cmodels[CMODEL_LAST] = {
		[CMODEL_LARGE] = "LARGE",
		[CMODEL_SMALL] = "SMALL",
		[CMODEL_TINY]  = "TINY",
	};
	const char *cmodel = cmodels[arch_cmodel];

	predefine("__aarch64__", 1, "1");

	if (cmodel)
		add_pre_buffer("#define __AARCH64_CMODEL_%s__ 1\n", cmodel);
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
