#include "symbol.h"
#include "target.h"
#include "machine.h"


static void predefine_arm(const struct target *self)
{
	predefine("__arm__", 1, "1");
	predefine("__VFP_FP__", 1, "1");

	switch (arch_fp_abi) {
	case FP_ABI_HARD:
		predefine("__ARM_PCS_VFP", 1, "1");
		break;
	case FP_ABI_SOFT:
		predefine("__SOFTFP__", 1, "1");
		/* fall-through */
	case FP_ABI_HYBRID:
		predefine("__ARM_PCS", 1, "1");
		break;
	}

	if (arch_big_endian)
		predefine("__ARMEB__", 0, "1");
	else
		predefine("__ARMEL__", 0, "1");
}

const struct target target_arm = {
	.mach = MACH_ARM,
	.bitness = ARCH_LP32,
	.big_endian = 0,
	.unsigned_char = 1,

	.wchar = &uint_ctype,

	.bits_in_longdouble = 64,
	.max_fp_alignment = 8,

	.predefine = predefine_arm,
};
