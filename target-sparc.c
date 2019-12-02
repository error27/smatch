#include "symbol.h"
#include "target.h"
#include "machine.h"


static void predefine_sparc(const struct target *self)
{
	predefine("__sparc__", 1, "1");
	predefine("__sparc", 1, "1");
}


static void init_sparc32(const struct target *target)
{
	if (arch_os == OS_SUNOS) {
		wint_ctype = &long_ctype;
		wchar_ctype = &long_ctype;

		bits_in_longdouble = 128;
		max_fp_alignment = 16;
	}
}

static void predefine_sparc32(const struct target *self)
{
	predefine_sparc(self);
}

const struct target target_sparc32 = {
	.mach = MACH_SPARC32,
	.bitness = ARCH_LP32,
	.big_endian = 1,
	.unsigned_char = 0,

	.bits_in_longdouble = 64,
	.max_fp_alignment = 8,

	.init = init_sparc32,
	.target_64bit = &target_sparc64,

	.predefine = predefine_sparc32,
};


static void predefine_sparc64(const struct target *self)
{
	predefine("__sparc_v9__", 1, "1");
	predefine("__sparcv9__", 1, "1");
	predefine("__sparcv9", 1, "1");
	predefine("__sparc64__", 1, "1");
	predefine("__arch64__", 1, "1");

	predefine_sparc(self);
}

const struct target target_sparc64 = {
	.mach = MACH_SPARC64,
	.bitness = ARCH_LP64,
	.big_endian = 1,
	.unsigned_char = 0,

	.target_32bit = &target_sparc32,

	.predefine = predefine_sparc64,
};
