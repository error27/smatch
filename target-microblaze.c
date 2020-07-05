#include "symbol.h"
#include "target.h"
#include "machine.h"


static void predefine_microblaze(const struct target *self)
{
	predefine("__MICROBLAZE__", 1, "1");
	predefine("__microblaze__", 1, "1");

	if (arch_big_endian)
		predefine("__MICROBLAZEEB__", 1, "1");
	else
		predefine("__MICROBLAZEEL__", 1, "1");
}

const struct target target_microblaze = {
	.mach = MACH_MICROBLAZE,
	.bitness = ARCH_LP32,
	.big_endian = true,

	.bits_in_longdouble = 64,

	.predefine = predefine_microblaze,
};
