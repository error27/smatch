#include "expression.h"

#ifndef   	SMATCH_CONSTANTS_H_
#define   	SMATCH_CONSTANTS_H_

static const sval_t int_zero = { .type = &int_ctype, .value = 0 };
static const sval_t int_one  = { .type = &int_ctype, .value = 1 };
static const sval_t err_min  = { .type = &int_ctype, .value = -4095 };
static const sval_t err_max  = { .type = &int_ctype, .value = -1 };
static const sval_t int_max  = { .type = &int_ctype, .value = INT_MAX };

#endif
