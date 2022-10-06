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

#include <string.h>
#include <openssl/evp.h>

#define MTAG_ALIAS_BIT (1ULL << 63)

unsigned long long str_to_llu_hash_helper(const char *str)
{
	unsigned char c[EVP_MAX_MD_SIZE];
	unsigned long long *tag = (unsigned long long *)&c;
	EVP_MD_CTX *mdctx;
	const EVP_MD *md;
	int len;

	len = strlen(str);

	mdctx = EVP_MD_CTX_create();
	md = EVP_sha1();

	EVP_DigestInit_ex(mdctx, md, NULL);
	EVP_DigestUpdate(mdctx, str, len);
	EVP_DigestFinal_ex(mdctx, c, NULL);
	EVP_MD_CTX_destroy(mdctx);

	/* I don't like negatives in the DB */
	*tag &= ~MTAG_ALIAS_BIT;

	return *tag;
}

