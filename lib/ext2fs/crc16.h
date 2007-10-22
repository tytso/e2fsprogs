/*
 *	crc16.h - CRC-16 routine
 *
 * Implements the standard CRC-16:
 *   Width 16
 *   Poly  0x8005 (x16 + x15 + x2 + 1)
 *   Init  0
 *
 * Copyright (c) 2005 Ben Gardner <bgardner@wabtec.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#ifndef __CRC16_H
#define __CRC16_H

#include <ext2fs/ext2_types.h>

extern __u16 const crc16_table[256];

#ifdef WORDS_BIGENDIAN
/* for an unknown reason, PPC treats __u16 as signed and keeps doing sign
 * extension on the value.  Instead, use only the low 16 bits of an
 * unsigned int for holding the CRC value to avoid this.
 */
typedef unsigned crc16_t;

static inline crc16_t crc16_byte(crc16_t crc, const unsigned char data)
{
	return (((crc >> 8) & 0xffU) ^ crc16_table[(crc ^ data) & 0xffU]) &
		0x0000ffffU;
}
#else
typedef __u16 crc16_t;

static inline crc16_t crc16_byte(crc16_t crc, const unsigned char data)
{
	return (crc >> 8) ^ crc16_table[(crc ^ data) & 0xff];
}
#endif

extern crc16_t crc16(crc16_t crc, const void *buffer, unsigned int len);

#endif /* __CRC16_H */
