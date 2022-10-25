/*
 * Copyright (C) 2022 Oracle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include <stdio.h>

unsigned long long str_to_llu_hash_helper(const char *str);

int main(int argc, char **argv)
{
	unsigned long long hash;

	if (argc != 2) {
		printf("Usage: sm_hash <string>\n");
		return -1;
	}

	hash = str_to_llu_hash_helper(argv[1]);

	printf("%llu\n", hash);

	return 0;
}
