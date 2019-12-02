#include "symbol.h"
#include "target.h"
#include "machine.h"


static void predefine_ppc(const struct target *self)
{
	predefine("__powerpc__", 1, "1");
	predefine("__powerpc", 1, "1");
	predefine("__ppc__", 1, "1");
	predefine("__PPC__", 1, "1");
	predefine("__PPC", 1, "1");
	predefine("_ARCH_PPC", 1, "1");
	if (arch_big_endian)
		predefine("_BIG_ENDIAN", 1, "1");
}


static void predefine_ppc32(const struct target *self)
{
	predefine_ppc(self);
}

const struct target target_ppc32 = {
	.mach = MACH_PPC32,
	.bitness = ARCH_LP32,
	.big_endian = 1,
	.unsigned_char = 1,

	.wchar = &long_ctype,

	.target_64bit = &target_ppc64,

	.predefine = predefine_ppc32,
};


static void predefine_ppc64(const struct target *self)
{
	predefine("__powerpc64__", 1, "1");
	predefine("__ppc64__", 1, "1");
	predefine("__PPC64__", 1, "1");
	predefine("_ARCH_PPC64", 1, "1");

	predefine_ppc(self);
}

const struct target target_ppc64 = {
	.mach = MACH_PPC64,
	.bitness = ARCH_LP64,
	.big_endian = 1,
	.unsigned_char = 1,
	.has_int128 = 1,

	.target_32bit = &target_ppc32,

	.predefine = predefine_ppc64,
};
