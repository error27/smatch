#include "symbol.h"
#include "target.h"
#include "machine.h"


static void init_openrisc(const struct target *self)
{
	wchar_ctype = &uint_ctype;
}

static void predefine_openrisc(const struct target *self)
{
	predefine_weak("__OR1K__");
	predefine_weak("__or1k__");
}

const struct target target_openrisc = {
	.mach = MACH_NDS32,
	.bitness = ARCH_LP32,
	.big_endian = true,

	.bits_in_longdouble = 64,

	.init = init_openrisc,
	.predefine = predefine_openrisc,
};
