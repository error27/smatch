// SPDX-License-Identifier: MIT
// Copyright (C) 2017-2020 Luc Van Oostenryck.

#include <stdio.h>

#include "lib.h"
#include "machine.h"
#include "symbol.h"

#define PTYPE_SIZEOF	(1U << 0)
#define PTYPE_T		(1U << 1)
#define PTYPE_MAX	(1U << 2)
#define PTYPE_MIN	(1U << 3)
#define PTYPE_WIDTH	(1U << 4)
#define PTYPE_TYPE	(1U << 5)
#define PTYPE_ALL	(PTYPE_MAX|PTYPE_SIZEOF|PTYPE_WIDTH)
#define PTYPE_ALL_T	(PTYPE_MAX|PTYPE_SIZEOF|PTYPE_WIDTH|PTYPE_T)


static void predefined_sizeof(const char *name, const char *suffix, unsigned bits)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "__SIZEOF_%s%s__", name, suffix);
	predefine(buf, 1, "%d", bits/8);
}

static void predefined_width(const char *name, unsigned bits)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "__%s_WIDTH__", name);
	predefine(buf, 1, "%d", bits);
}

static void predefined_max(const char *name, struct symbol *type)
{
	const char *suffix = builtin_type_suffix(type);
	unsigned bits = type->bit_size - is_signed_type(type);
	unsigned long long max = bits_mask(bits);
	char buf[32];

	snprintf(buf, sizeof(buf), "__%s_MAX__", name);
	predefine(buf, 1, "%#llx%s", max, suffix);
}

static void predefined_min(const char *name, struct symbol *type)
{
	const char *suffix = builtin_type_suffix(type);
	char buf[32];

	snprintf(buf, sizeof(buf), "__%s_MIN__", name);

	if (is_signed_type(type))
		add_pre_buffer("#weak_define %s (-__%s_MAX__ - 1)\n", buf, name);
	else
		predefine(buf, 1, "0%s", suffix);
}

static void predefined_type(const char *name, struct symbol *type)
{
	const char *typename = builtin_typename(type);
	add_pre_buffer("#weak_define __%s_TYPE__ %s\n", name, typename);
}

static void predefined_ctype(const char *name, struct symbol *type, int flags)
{
	unsigned bits = type->bit_size;

	if (flags & PTYPE_SIZEOF) {
		const char *suffix = (flags & PTYPE_T) ? "_T" : "";
		predefined_sizeof(name, suffix, bits);
	}
	if (flags & PTYPE_MAX)
		predefined_max(name, type);
	if (flags & PTYPE_MIN)
		predefined_min(name, type);
	if (flags & PTYPE_TYPE)
		predefined_type(name, type);
	if (flags & PTYPE_WIDTH)
		predefined_width(name, bits);
}

void predefined_macros(void)
{
	predefine("__CHECKER__", 0, "1");
	predefine("__GNUC__", 1, "%d", gcc_major);
	predefine("__GNUC_MINOR__", 1, "%d", gcc_minor);
	predefine("__GNUC_PATCHLEVEL__", 1, "%d", gcc_patchlevel);

	predefine("__STDC__", 1, "1");
	predefine("__STDC_HOSTED__", 0, fhosted ? "1" : "0");
	switch (standard) {
	default:
		break;

	case STANDARD_C94:
		predefine("__STDC_VERSION__", 1, "199409L");
		break;

	case STANDARD_C99:
	case STANDARD_GNU99:
		predefine("__STDC_VERSION__", 1, "199901L");
		break;

	case STANDARD_C11:
	case STANDARD_GNU11:
		predefine("__STDC_VERSION__", 1, "201112L");
		break;
	case STANDARD_C17:
	case STANDARD_GNU17:
		predefine("__STDC_VERSION__", 1, "201710L");
		break;
	}
	if (!(standard & STANDARD_GNU) && (standard != STANDARD_NONE))
		predefine("__STRICT_ANSI__", 1, "1");
	if (standard >= STANDARD_C11) {
		predefine("__STDC_NO_ATOMICS__", 1, "1");
		predefine("__STDC_NO_COMPLEX__", 1, "1");
		predefine("__STDC_NO_THREADS__", 1, "1");
	}

	predefine("__CHAR_BIT__", 1, "%d", bits_in_char);
	if (funsigned_char)
		predefine("__CHAR_UNSIGNED__", 1, "1");

	predefined_ctype("SHORT",     &short_ctype, PTYPE_SIZEOF);
	predefined_ctype("SHRT",      &short_ctype, PTYPE_MAX|PTYPE_WIDTH);
	predefined_ctype("SCHAR",     &schar_ctype, PTYPE_MAX|PTYPE_WIDTH);
	predefined_ctype("WCHAR",      wchar_ctype, PTYPE_ALL_T|PTYPE_MIN|PTYPE_TYPE);
	predefined_ctype("WINT",        wint_ctype, PTYPE_ALL_T|PTYPE_MIN|PTYPE_TYPE);
	predefined_ctype("CHAR16",   &ushort_ctype, PTYPE_TYPE);
	predefined_ctype("CHAR32",    uint32_ctype, PTYPE_TYPE);

	predefined_ctype("INT",         &int_ctype, PTYPE_ALL);
	predefined_ctype("LONG",       &long_ctype, PTYPE_ALL);
	predefined_ctype("LONG_LONG", &llong_ctype, PTYPE_ALL);

	predefined_ctype("INT8",      &schar_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT8",     &uchar_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INT16",     &short_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT16",   &ushort_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INT32",      int32_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT32",    uint32_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INT64",      int64_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT64",    uint64_ctype, PTYPE_MAX|PTYPE_TYPE);

	predefined_ctype("INTMAX",    intmax_ctype, PTYPE_MAX|PTYPE_TYPE|PTYPE_WIDTH);
	predefined_ctype("UINTMAX",  uintmax_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INTPTR",   ssize_t_ctype, PTYPE_MAX|PTYPE_TYPE|PTYPE_WIDTH);
	predefined_ctype("UINTPTR",   size_t_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("PTRDIFF",  ssize_t_ctype, PTYPE_ALL_T|PTYPE_TYPE);
	predefined_ctype("SIZE",      size_t_ctype, PTYPE_ALL_T|PTYPE_TYPE);
	predefined_ctype("POINTER",     &ptr_ctype, PTYPE_SIZEOF);

	predefined_sizeof("FLOAT", "", bits_in_float);
	predefined_sizeof("DOUBLE", "", bits_in_double);
	predefined_sizeof("LONG_DOUBLE", "", bits_in_longdouble);

	if (arch_target->has_int128)
		predefined_sizeof("INT128", "", 128);

	predefine("__ORDER_LITTLE_ENDIAN__", 1, "1234");
	predefine("__ORDER_BIG_ENDIAN__", 1, "4321");
	predefine("__ORDER_PDP_ENDIAN__", 1, "3412");
	if (arch_big_endian) {
		predefine("__BIG_ENDIAN__", 1, "1");
		predefine("__BYTE_ORDER__", 1, "__ORDER_BIG_ENDIAN__");
	} else {
		predefine("__LITTLE_ENDIAN__", 1, "1");
		predefine("__BYTE_ORDER__", 1, "__ORDER_LITTLE_ENDIAN__");
	}

	if (optimize_level)
		predefine("__OPTIMIZE__", 0, "1");
	if (optimize_size)
		predefine("__OPTIMIZE_SIZE__", 0, "1");

	predefine("__PRAGMA_REDEFINE_EXTNAME", 1, "1");

	// Temporary hacks
	predefine("__extension__", 0, NULL);
	predefine("__pragma__", 0, NULL);

	switch (arch_m64) {
	case ARCH_LP32:
		break;
	case ARCH_X32:
		predefine("__ILP32__", 1, "1");
		predefine("_ILP32", 1, "1");
		break;
	case ARCH_LP64:
		predefine("__LP64__", 1, "1");
		predefine("_LP64", 1, "1");
		break;
	case ARCH_LLP64:
		predefine("__LLP64__", 1, "1");
		break;
	}

	if (fpic) {
		predefine("__pic__", 0, "%d", fpic);
		predefine("__PIC__", 0, "%d", fpic);
	}
	if (fpie) {
		predefine("__pie__", 0, "%d", fpie);
		predefine("__PIE__", 0, "%d", fpie);
	}

	if (arch_target->predefine)
		arch_target->predefine(arch_target);

	if (arch_os >= OS_UNIX && arch_os != OS_DARWIN) {
		predefine("__unix__", 1, "1");
		predefine("__unix", 1, "1");
		predefine_nostd("unix");
	}

	switch (arch_os) {
	case OS_LINUX:
		predefine("__linux__", 1, "1");
		predefine("__linux", 1, "1");
		break;
	case OS_SUNOS:
		predefine("__sun__", 1, "1");
		predefine("__sun", 1, "1");
		predefine_nostd("sun");
		predefine("__svr4__", 1, "1");
		break;
	}
}
