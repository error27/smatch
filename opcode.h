#ifndef OPCODE_H
#define OPCODE_H

#include "symbol.h"

extern const struct opcode_table {
	int	negate:8;
	int	swap:8;
	int	to_float:8;
} opcode_table[];


static inline int opcode_float(int opcode, struct symbol *type)
{
	if (!type || !is_float_type(type))
		return opcode;
	return opcode_table[opcode].to_float;
}

#endif
