/*
 * Copyright (C) 2017 Luc Van Oostenryck
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "linearize.h"

const struct opcode_table opcode_table[OP_LAST] = {
	[OP_SET_EQ] = {	.negate = OP_SET_NE, .swap = OP_SET_EQ, .to_float = OP_FCMP_OEQ, },
	[OP_SET_NE] = {	.negate = OP_SET_EQ, .swap = OP_SET_NE, .to_float = OP_FCMP_UNE, },
	[OP_SET_LT] = {	.negate = OP_SET_GE, .swap = OP_SET_GT, .to_float = OP_FCMP_OLT, },
	[OP_SET_LE] = {	.negate = OP_SET_GT, .swap = OP_SET_GE, .to_float = OP_FCMP_OLE, },
	[OP_SET_GE] = {	.negate = OP_SET_LT, .swap = OP_SET_LE, .to_float = OP_FCMP_OGE, },
	[OP_SET_GT] = {	.negate = OP_SET_LE, .swap = OP_SET_LT, .to_float = OP_FCMP_OGT, },
	[OP_SET_B ] = {	.negate = OP_SET_AE, .swap = OP_SET_A , .to_float = OP_FCMP_OLT, },
	[OP_SET_BE] = {	.negate = OP_SET_A , .swap = OP_SET_AE, .to_float = OP_FCMP_OLE, },
	[OP_SET_AE] = {	.negate = OP_SET_B , .swap = OP_SET_BE, .to_float = OP_FCMP_OGE, },
	[OP_SET_A ] = {	.negate = OP_SET_BE, .swap = OP_SET_B , .to_float = OP_FCMP_OGT, },

	[OP_FCMP_ORD] = { .negate = OP_FCMP_UNO, .swap = OP_FCMP_ORD, },
	[OP_FCMP_UNO] = { .negate = OP_FCMP_ORD, .swap = OP_FCMP_UNO, },

	[OP_FCMP_OEQ] = { .negate = OP_FCMP_UNE, .swap = OP_FCMP_OEQ, },
	[OP_FCMP_ONE] = { .negate = OP_FCMP_UEQ, .swap = OP_FCMP_ONE, },
	[OP_FCMP_UEQ] = { .negate = OP_FCMP_ONE, .swap = OP_FCMP_UEQ, },
	[OP_FCMP_UNE] = { .negate = OP_FCMP_OEQ, .swap = OP_FCMP_UNE, },

	[OP_FCMP_OLT] = { .negate = OP_FCMP_UGE, .swap = OP_FCMP_OGT, },
	[OP_FCMP_OLE] = { .negate = OP_FCMP_UGT, .swap = OP_FCMP_OGE, },
	[OP_FCMP_OGE] = { .negate = OP_FCMP_ULT, .swap = OP_FCMP_OLE, },
	[OP_FCMP_OGT] = { .negate = OP_FCMP_ULE, .swap = OP_FCMP_OLT, },

	[OP_FCMP_ULT] = { .negate = OP_FCMP_OGE, .swap = OP_FCMP_UGT, },
	[OP_FCMP_ULE] = { .negate = OP_FCMP_OGT, .swap = OP_FCMP_UGE, },
	[OP_FCMP_UGE] = { .negate = OP_FCMP_OLT, .swap = OP_FCMP_ULE, },
	[OP_FCMP_UGT] = { .negate = OP_FCMP_OLE, .swap = OP_FCMP_ULT, },

	[OP_ADD] = {	.to_float = OP_FADD, },
	[OP_SUB] = {	.to_float = OP_FSUB, },
	[OP_MUL] = {	.to_float = OP_FMUL, },
	[OP_DIVS] = {	.to_float = OP_FDIV, },
	[OP_DIVU] = {	.to_float = OP_FDIV, },
	[OP_NEG] = {	.to_float = OP_FNEG, },
};
