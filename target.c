#include <stdio.h>
#include <string.h>

#include "symbol.h"
#include "target.h"
#include "machine.h"

struct symbol *size_t_ctype = &ulong_ctype;
struct symbol *ssize_t_ctype = &long_ctype;
struct symbol *intmax_ctype = &long_ctype;
struct symbol *uintmax_ctype = &ulong_ctype;
struct symbol *int64_ctype = &long_ctype;
struct symbol *uint64_ctype = &ulong_ctype;
struct symbol *int32_ctype = &int_ctype;
struct symbol *uint32_ctype = &uint_ctype;
struct symbol *wchar_ctype = &int_ctype;
struct symbol *wint_ctype = &uint_ctype;

/*
 * For "__attribute__((aligned))"
 */
int max_alignment = 16;

/*
 * Integer data types
 */
int bits_in_bool = 1;
int bits_in_char = 8;
int bits_in_short = 16;
int bits_in_int = 32;
int bits_in_long = 64;
int bits_in_longlong = 64;
int bits_in_longlonglong = 128;

int max_int_alignment = 8;

/*
 * Floating point data types
 */
int bits_in_float = 32;
int bits_in_double = 64;
int bits_in_longdouble = 128;

int max_fp_alignment = 16;

/*
 * Pointer data type
 */
int bits_in_pointer = 64;
int pointer_alignment = 8;

/*
 * Enum data types
 */
int bits_in_enum = 32;
int enum_alignment = 4;


static const struct target *targets[] = {
	[MACH_ARM] =		&target_arm,
	[MACH_ARM64] =		&target_arm64,
	[MACH_I386] =		&target_i386,
	[MACH_BFIN] =		&target_bfin,
	[MACH_X86_64] =		&target_x86_64,
	[MACH_MIPS32] =		&target_mips32,
	[MACH_MIPS64] =		&target_mips64,
	[MACH_NIOS2] =		&target_nios2,
	[MACH_PPC32] =		&target_ppc32,
	[MACH_PPC64] =		&target_ppc64,
	[MACH_RISCV32] =	&target_riscv32,
	[MACH_RISCV64] =	&target_riscv64,
	[MACH_S390] =		&target_s390,
	[MACH_S390X] =		&target_s390x,
	[MACH_SPARC32] =	&target_sparc32,
	[MACH_SPARC64] =	&target_sparc64,
	[MACH_M68K] =		&target_m68k,
	[MACH_UNKNOWN] =	&target_default,
};
const struct target *arch_target = &target_default;

enum machine target_parse(const char *name)
{
	static const struct arch {
		const char *name;
		enum machine mach;
		char bits;
	} archs[] = {
		{ "aarch64",	MACH_ARM64,	64, },
		{ "arm64",	MACH_ARM64,	64, },
		{ "arm",	MACH_ARM,	32, },
		{ "i386",	MACH_I386,	32, },
		{ "bfin",	MACH_BFIN,	32, },
		{ "m68k",	MACH_M68K,	32, },
		{ "mips",	MACH_MIPS32,	0,  },
		{ "nios2",	MACH_NIOS2,	32, },
		{ "powerpc",	MACH_PPC32,	0,  },
		{ "ppc",	MACH_PPC32,	0,  },
		{ "riscv",	MACH_RISCV32,	0,  },
		{ "s390x",	MACH_S390X,	64, },
		{ "s390",	MACH_S390,	32, },
		{ "sparc",	MACH_SPARC32,	0,  },
		{ "x86_64",	MACH_X86_64,	64, },
		{ "x86-64",	MACH_X86_64,	64, },
		{ NULL },
	};
	const struct arch *p;

	for (p = &archs[0]; p->name; p++) {
		size_t len = strlen(p->name);
		if (strncmp(p->name, name, len) == 0) {
			enum machine mach = p->mach;
			const char *suf = name + len;
			int bits = p->bits;

			if (bits == 0) {
				if (!strcmp(suf, "") || !strcmp(suf, "32")) {
					;
				} else if (!strcmp(suf, "64")) {
					mach += 1;
				} else {
					die("invalid architecture: %s", name);
				}
			} else {
				if (strcmp(suf, ""))
					die("invalid architecture: %s", name);
			}

			return mach;
		}
	}

	return MACH_UNKNOWN;
}


void target_config(enum machine mach)
{
	const struct target *target = targets[mach];

	arch_target = target;
	arch_m64 = target->bitness;
	arch_big_endian = target->big_endian;

	funsigned_char = target->unsigned_char;
}


void target_init(void)
{
	const struct target *target = arch_target;

	switch (arch_m64) {
	case ARCH_LP32:
		max_int_alignment = 4;
		/* fallthrough */
	case ARCH_X32:
		bits_in_long = 32;
		bits_in_pointer = 32;
		pointer_alignment = 4;
		size_t_ctype = &uint_ctype;
		ssize_t_ctype = &int_ctype;
		int64_ctype = &llong_ctype;
		uint64_ctype = &ullong_ctype;
		intmax_ctype = &llong_ctype;
		uintmax_ctype = &ullong_ctype;
		if (target->target_32bit)
			target = target->target_32bit;
		break;

	case ARCH_LLP64:
		bits_in_long = 32;
		size_t_ctype = &ullong_ctype;
		ssize_t_ctype = &llong_ctype;
		int64_ctype = &llong_ctype;
		uint64_ctype = &ullong_ctype;
		intmax_ctype = &llong_ctype;
		uintmax_ctype = &ullong_ctype;
		/* fallthrough */
	case ARCH_LP64:
		if (target->target_64bit)
			target = target->target_64bit;
		break;
	}
	arch_target = target;

	if (fpie > fpic)
		fpic = fpie;

	if (target->wchar)
		wchar_ctype = target->wchar;
	if (target->wint)
		wint_ctype = target->wint;
	if (target->bits_in_longdouble)
		bits_in_longdouble = target->bits_in_longdouble;
	if (target->max_fp_alignment)
		max_fp_alignment = target->max_fp_alignment;

	if (target->init)
		target->init(target);

	if (arch_msize_long || target->size_t_long) {
		size_t_ctype = &ulong_ctype;
		ssize_t_ctype = &long_ctype;
	}
	if (fshort_wchar)
		wchar_ctype = &ushort_ctype;
}
