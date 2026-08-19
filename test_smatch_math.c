/*
 * Copyright (C) 2026 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "smatch.h"

#define DEFAULT_RANGE_TESTS 20000
#define DEFAULT_VALUE_TESTS 5000

struct basic_type {
	const char *name;
	struct symbol *type;
};

struct math_op {
	const char *name;
	int op;
};

struct failure_ranges {
	struct range_list *left;
	sval_t output_max;
	sval_t output_min;
	struct range_list *outputs;
	struct range_list *right;
};

static struct basic_type basic_types[] = {
	{ "char", &char_ctype },
	{ "int", &int_ctype },
	{ "long", &long_ctype },
	{ "long long", &llong_ctype },
	{ "signed char", &schar_ctype },
	{ "short", &short_ctype },
	{ "unsigned char", &uchar_ctype },
	{ "unsigned int", &uint_ctype },
	{ "unsigned long", &ulong_ctype },
	{ "unsigned long long", &ullong_ctype },
	{ "unsigned short", &ushort_ctype },
};

static struct math_op math_ops[] = {
	{ "%", '%' },
	{ "&", '&' },
	{ "*", '*' },
	{ "+", '+' },
	{ "-", '-' },
	{ "/", '/' },
	{ "<<", SPECIAL_LEFTSHIFT },
	{ ">>", SPECIAL_RIGHTSHIFT },
	{ "^", '^' },
	{ "|", '|' },
};

static uint64_t random_state;

extern void clear_data_range_alloc(void);

static uint64_t random_u64(void)
{
	uint64_t x = random_state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	random_state = x;
	return x * UINT64_C(2685821657736338717);
}

/* A limit of zero represents all 2^64 possible values. */
static uint64_t random_below(uint64_t limit)
{
	if (!limit)
		return random_u64();
	return random_u64() % limit;
}

static sval_t random_sval(struct symbol *type)
{
	sval_t ret = { .type = &ullong_ctype };

	ret.uvalue = random_u64();
	return sval_cast(type, ret);
}

static sval_t random_sval_below(struct symbol *type, uint64_t limit)
{
	sval_t ret = { .type = &ullong_ctype };

	ret.uvalue = random_below(limit);
	return sval_cast(type, ret);
}

static struct range_list *random_rl(struct symbol *type, int op, bool right,
				    unsigned int shift_limit)
{
	struct range_list *rl = NULL;
	unsigned int i;
	unsigned int nr_ranges = random_below(3) + 1;

	for (i = 0; i < nr_ranges; i++) {
		sval_t min;
		sval_t max;

		if (right && (op == SPECIAL_LEFTSHIFT ||
			      op == SPECIAL_RIGHTSHIFT)) {
			min = random_sval_below(type, shift_limit);
			max = random_sval_below(type, shift_limit);
		} else {
			min = random_sval(type);
			max = random_sval(type);
		}
		if (sval_cmp(min, max) > 0) {
			sval_t tmp = min;

			min = max;
			max = tmp;
		}
		add_range(&rl, min, max);
	}

	if (right && (op == '/' || op == '%')) {
		sval_t zero = sval_type_val(type, 0);

		rl = remove_range(rl, zero, zero);
		if (!rl)
			return random_rl(type, op, right, shift_limit);
	}

	return rl;
}

static struct data_range *random_data_range(struct range_list *rl)
{
	struct data_range *drange;
	unsigned int target;
	unsigned int i = 0;

	target = random_below(ptr_list_size((struct ptr_list *)rl));
	FOR_EACH_PTR(rl, drange) {
		if (i++ == target)
			return drange;
	} END_FOR_EACH_PTR(drange);

	abort();
}

static sval_t random_sval_from_rl(struct range_list *rl)
{
	struct data_range *drange = random_data_range(rl);
	uint64_t count;
	sval_t ret = drange->min;

	count = drange->max.uvalue - drange->min.uvalue + 1;
	ret.uvalue += random_below(count);
	return ret;
}

static struct data_range *find_data_range(struct range_list *rl, sval_t sval)
{
	struct data_range *drange;

	FOR_EACH_PTR(rl, drange) {
		if (sval_cmp(drange->min, sval) <= 0 &&
		    sval_cmp(drange->max, sval) >= 0)
			return drange;
	} END_FOR_EACH_PTR(drange);

	abort();
}

static sval_t next_sval(sval_t sval, int direction)
{
	if (direction < 0)
		sval.uvalue--;
	else
		sval.uvalue++;
	return sval;
}

static void walk_failures(struct failure_ranges *failures,
			  struct range_list *result, sval_t left,
			  const struct math_op *math_op, sval_t right,
			  struct range_list *source, bool walk_left,
			  int direction)
{
	struct data_range *drange;
	struct range_list **inputs;
	sval_t current;
	sval_t input_max;
	sval_t input_min;
	sval_t output;
	unsigned int i;

	current = walk_left ? left : right;
	input_max = current;
	input_min = current;
	drange = find_data_range(source, current);
	inputs = walk_left ? &failures->left : &failures->right;

	for (i = 0; i < 100000; i++) {
		if (direction < 0 && sval_cmp(current, drange->min) == 0)
			break;
		if (direction > 0 && sval_cmp(current, drange->max) == 0)
			break;

		current = next_sval(current, direction);
		if (walk_left)
			output = sval_binop(current, math_op->op, right);
		else
			output = sval_binop(left, math_op->op, current);
		if (rl_has_sval(result, output))
			break;

		if (sval_cmp(current, input_min) < 0)
			input_min = current;
		if (sval_cmp(current, input_max) > 0)
			input_max = current;
		if (sval_cmp(output, failures->output_min) < 0)
			failures->output_min = output;
		if (sval_cmp(output, failures->output_max) > 0)
			failures->output_max = output;
	}

	add_range(inputs, input_min, input_max);
}

static void find_failure_ranges(struct failure_ranges *failures,
				struct range_list *left,
				const struct math_op *math_op,
				struct range_list *result,
				struct range_list *right, sval_t left_sval,
				sval_t right_sval, sval_t output)
{
	add_range(&failures->left, left_sval, left_sval);
	add_range(&failures->right, right_sval, right_sval);
	failures->output_max = output;
	failures->output_min = output;

	walk_failures(failures, result, left_sval, math_op, right_sval,
		      left, true, -1);
	walk_failures(failures, result, left_sval, math_op, right_sval,
		      left, true, 1);
	walk_failures(failures, result, left_sval, math_op, right_sval,
		      right, false, -1);
	walk_failures(failures, result, left_sval, math_op, right_sval,
		      right, false, 1);
	add_range(&failures->outputs, failures->output_min,
		  failures->output_max);
}

static void print_c_sval(sval_t sval)
{
	if (sval_unsigned(sval)) {
		printf("%lluULL", sval.uvalue);
		return;
	}
	if (sval.value == LLONG_MIN) {
		printf("(-9223372036854775807LL - 1)");
		return;
	}
	printf("%lldLL", sval.value);
}

static void print_range_test(const char *name, const char *type_name,
			     struct range_list *rl)
{
	struct data_range *drange;
	bool first = true;

	printf("bool %s_in_range(%s val)\n", name, type_name);
	printf("{\n");
	printf("\tif (");
	FOR_EACH_PTR(rl, drange) {
		if (!first)
			printf(" ||\n\t    ");
		first = false;
		if (sval_cmp(drange->min, drange->max) == 0) {
			printf("val == ");
			print_c_sval(drange->min);
		} else {
			printf("(val >= ");
			print_c_sval(drange->min);
			printf(" && val <= ");
			print_c_sval(drange->max);
			printf(")");
		}
	} END_FOR_EACH_PTR(drange);
	printf(")\n");
	printf("\t\treturn true;\n");
	printf("\treturn false;\n");
	printf("}\n\n");
}

static void print_test_case(const struct failure_ranges *failures,
			    const struct basic_type *left_type,
			    struct range_list *left,
			    const struct math_op *math_op,
			    struct range_list *result,
			    const struct basic_type *right_type,
			    struct range_list *right, sval_t left_sval,
			    sval_t right_sval, sval_t actual, uint64_t seed,
			    unsigned long range_test, unsigned long value_test)
{
	printf("#include <stdbool.h>\n");
	printf("#include <sys/types.h>\n");
	printf("#include \"check_debug.h\"\n\n");
	printf("/*\n\n");
	printf("./test_smatch_math %" PRIu64 "\n", seed);
	printf("error: result outside range\n");
	printf("seed: %" PRIu64 "\n", seed);
	printf("range test: %lu\n", range_test);
	printf("value test: %lu\n", value_test);
	printf("operation: %s\n", math_op->name);
	printf("left type: %s\n", left_type->name);
	printf("left range: %s\n", show_rl(left));
	printf("left value: %s\n", sval_to_str(left_sval));
	printf("right type: %s\n", right_type->name);
	printf("right range: %s\n", show_rl(right));
	printf("right value: %s\n", sval_to_str(right_sval));
	printf("result range: %s\n", show_rl(result));
	printf("actual result: %s\n", sval_to_str(actual));
	printf("left inputs that don't work (right = %s): %s\n",
	       sval_to_str(right_sval), show_rl(failures->left));
	printf("right inputs that don't work (left = %s): %s\n",
	       sval_to_str(left_sval), show_rl(failures->right));
	printf("out-of-bounds outputs: %s\n", show_rl(failures->outputs));
	printf("\n*/\n\n");

	print_range_test("left", left_type->name, left);
	print_range_test("right", right_type->name, right);
	printf("int func(%s left, %s right)\n", left_type->name,
	       right_type->name);
	printf("{\n");
	printf("\t%s result;\n\n", type_to_str(actual.type));
	printf("\tif (!left_in_range(left))\n");
	printf("\t\treturn;\n");
	printf("\tif (!right_in_range(right))\n");
	printf("\t\treturn;\n\n");
	printf("\tresult = left %s right;\n", math_op->name);
	printf("\t/* left inputs that don't work (right = %s): %s\n",
	       sval_to_str(right_sval), show_rl(failures->left));
	printf("\t * right inputs that don't work (left = %s): %s\n",
	       sval_to_str(left_sval), show_rl(failures->right));
	printf("\t * out-of-bounds outputs: %s\n",
	       show_rl(failures->outputs));
	printf("\t */\n");
	printf("\t__smatch_implied(result);\n");
	printf("}\n");
}

static int check_one(const struct math_op *math_op, uint64_t seed,
		     unsigned long range_test, unsigned long value_tests)
{
	struct basic_type *left_type;
	struct basic_type *right_type;
	struct range_list *left;
	struct range_list *right;
	struct range_list *result;
	unsigned int shift_limit;
	unsigned long i;

	left_type = &basic_types[random_below(ARRAY_SIZE(basic_types))];
	right_type = &basic_types[random_below(ARRAY_SIZE(basic_types))];
	shift_limit = type_bits(left_type->type);
	if (shift_limit < type_bits(&int_ctype))
		shift_limit = type_bits(&int_ctype);
	left = random_rl(left_type->type, math_op->op, false, shift_limit);
	right = random_rl(right_type->type, math_op->op, true, shift_limit);
	result = rl_binop(left, math_op->op, right);

	for (i = 0; i < value_tests; i++) {
		sval_t left_sval = random_sval_from_rl(left);
		sval_t right_sval = random_sval_from_rl(right);
		sval_t actual = sval_binop(left_sval, math_op->op, right_sval);
		struct failure_ranges failures = {};

		if (rl_has_sval(result, actual))
			continue;

		find_failure_ranges(&failures, left, math_op, result, right,
				    left_sval, right_sval, actual);
		print_test_case(&failures, left_type, left, math_op, result,
				right_type, right, left_sval, right_sval,
				actual, seed, range_test, i);
		return -1;
	}

	free_all_rl();
	clear_data_range_alloc();
	return 0;
}

static unsigned long parse_count(const char *str, const char *name)
{
	char *end;
	unsigned long ret;

	errno = 0;
	ret = strtoul(str, &end, 0);
	if (errno || !str[0] || *end) {
		fprintf(stderr, "invalid %s: %s\n", name, str);
		exit(EXIT_FAILURE);
	}
	return ret;
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	uint64_t seed = time(NULL);
	unsigned long range_tests = DEFAULT_RANGE_TESTS;
	unsigned long value_tests = DEFAULT_VALUE_TESTS;
	unsigned long i;
	unsigned int op;

	if (argc > 4) {
		fprintf(stderr, "usage: %s [seed [range-tests [value-tests]]]\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	if (argc > 1)
		seed = parse_count(argv[1], "seed");
	if (argc > 2)
		range_tests = parse_count(argv[2], "range test count");
	if (argc > 3)
		value_tests = parse_count(argv[3], "value test count");
	if (!seed)
		seed = 1;
	random_state = seed;

	/* Initialize Sparse's target-dependent integer types. */
	sparse_initialize(1, argv, &filelist);

	for (op = 0; op < ARRAY_SIZE(math_ops); op++) {
		for (i = 0; i < range_tests; i++) {
			if (check_one(&math_ops[op], seed, i, value_tests))
				return EXIT_FAILURE;
		}
	}

	printf("seed: %" PRIu64 "\n", seed);
	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
