/*
 *     KeeLoq implementation for Atmel AVR
 *     Copyright (C) 2005 Jiri Pittner <jiri@pittnerovi.com>
 *
 *	   http://www.pittnerovi.cz/jiri/hobby/electronics/keeloq/index.html
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.

	   This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
*/

#include "keeloq_crypt.h"

static uint8_t NLF[4] = { 0x2e,0x74,0x5c,0x3a };

void keeloq_decrypt(uint32_t* code, uint64_t* key)
{
	uint16_t i;
	uint64_t keybak;
	keybak = *key;
	for (i = 0; i < 528; ++i)
	{
		uint8_t nlfshift, nlfind, tmp, r;
		nlfshift = *code & 1;
		if (*code & 256) nlfshift |= 2;
		if (*code & 0x080000) nlfshift |= 4;
		nlfind = (*code & 0x2000000) ? 1 : 0;
		if (*code & 0x40000000) nlfind |= 2;
		r = (NLF[nlfind] >> nlfshift) & 1;
		if (*key & 0x8000) r ^= 1;
		if (*code & 0x80000000) r ^= 1;
		if (*code & 0x8000) r ^= 1;
		tmp = *key >> 63; *key <<= 1; *key |= tmp; //rotate key
		*code <<= 1; *code |= r; //shift code
	}
	*key = keybak;
}

void keeloq_encrypt(uint32_t* code, uint64_t* key)
{
	uint16_t i;
	uint64_t keybak;
	keybak = *key;
	for (i = 0;i < 16;++i)
	{
		uint8_t tmp; tmp = *key >> 63; *key <<= 1; *key |= tmp;
	}

	for (i = 0; i < 528; ++i)
	{
		uint8_t nlfshift, nlfind, tmp, r;
		nlfshift = *code & 2 ? 1 : 0;
		if (*code & 512) nlfshift |= 2;
		if (*code & 0x100000) nlfshift |= 4;
		nlfind = (*code & 0x4000000) ? 1 : 0;
		if (*code & 0x80000000) nlfind |= 2;
		r = (NLF[nlfind] >> nlfshift) & 1;
		if (*key & 0x10000) r ^= 1;
		if (*code & 1) r ^= 1;
		if (*code & 0x10000) r ^= 1;
		tmp = *key & 1; *key >>= 1; *key |= ((uint64_t)tmp) << 63; //rotate key
		*code >>= 1; *code |= ((uint32_t)r) << 31; //shift code
	}
	*key = keybak;
}
