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
	[OP_SET_EQ] = {	.negate = OP_SET_NE, },
	[OP_SET_NE] = {	.negate = OP_SET_EQ, },
	[OP_SET_LT] = {	.negate = OP_SET_GE, },
	[OP_SET_LE] = {	.negate = OP_SET_GT, },
	[OP_SET_GE] = {	.negate = OP_SET_LT, },
	[OP_SET_GT] = {	.negate = OP_SET_LE, },
	[OP_SET_B ] = {	.negate = OP_SET_AE, },
	[OP_SET_BE] = {	.negate = OP_SET_A , },
	[OP_SET_AE] = {	.negate = OP_SET_B , },
	[OP_SET_A ] = {	.negate = OP_SET_BE, },
};
