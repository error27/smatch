#include "symbol.h"
#include "target.h"
#include "machine.h"


static void init_x86(const struct target *target)
{
	switch (arch_os) {
	case OS_CYGWIN:
		wchar_ctype = &ushort_ctype;
		break;
	case OS_DARWIN:
		int64_ctype = &llong_ctype;
		uint64_ctype = &ullong_ctype;
		wint_ctype = &int_ctype;
		break;
	case OS_FREEBSD:
		wint_ctype = &int_ctype;
		break;
	case OS_OPENBSD:
		wchar_ctype = &int_ctype;
		wint_ctype = &int_ctype;
		break;
	}
}

const struct target target_i386 = {
	.mach = MACH_I386,
	.bitness = ARCH_LP32,
	.big_endian = 0,
	.unsigned_char = 0,

	.wchar = &long_ctype,
	.bits_in_longdouble = 96,
	.max_fp_alignment = 4,

	.init = init_x86,
	.target_64bit = &target_x86_64,
};

const struct target target_x86_64 = {
	.mach = MACH_X86_64,
	.bitness = ARCH_LP64,
	.big_endian = 0,
	.unsigned_char = 0,

	.bits_in_longdouble = 128,
	.max_fp_alignment = 16,

	.init = init_x86,
	.target_32bit = &target_i386,
};
