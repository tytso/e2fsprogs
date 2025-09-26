// SPDX-License-Identifier: GPL-2.0-only
/*
 * Using hardware provided CRC32 instruction to accelerate the CRC32 disposal.
 * CRC32C polynomial:0x1EDC6F41(BE)/0x82F63B78(LE)
 * CRC32 is a new instruction in Intel SSE4.2, the reference can be found at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2A: Instruction Set Reference, A-M
 *
 * Copyright (C) 2008 Intel Corporation
 * Authors: Austin Zhang <austin_zhang@linux.intel.com>
 *          Kent Liu <kent.liu@intel.com>
 *          Dinglan Peng <pengdinglan@gmail.com>
 */
#if defined(__x86_64__) || defined(__i386__)
#include <stdint.h>
#include <stddef.h>
#include <cpuid.h>

#define CHKSUM_BLOCK_SIZE	1
#define CHKSUM_DIGEST_SIZE	4

#define SCALE_F	sizeof(unsigned long)

#ifdef __x86_64__
#define CRC32_INST "crc32q %1, %q0"
#else
#define CRC32_INST "crc32l %1, %0"
#endif

#ifdef __x86_64__
unsigned int crc_pcl(const uint8_t *buffer, int len,
		     unsigned int crc_init);
#endif

#define HAVE_SSE42	1
#define NO_SSE42	2
#define HAVE_PCL	4
#define NO_PCL		8

static int support_features;

static void detect_features(void) {
	unsigned int eax, ebx, ecx, edx;
	int features = 0;

	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		features = NO_SSE42 | NO_PCL;
	if (ecx & bit_SSE4_2)
		features |= HAVE_SSE42;
	else
		features |= NO_SSE42;
#ifdef __x86_64__
	if (ecx & bit_PCLMUL)
		features |= HAVE_PCL;
	else
		features |= NO_PCL;
#endif

	support_features = features;
}

static uint32_t crc32c_intel_le_hw_byte(uint32_t crc, unsigned char const *data, size_t length)
{
	while (length--) {
		asm("crc32b %1, %0"
		    : "+r" (crc) : "rm" (*data));
		data++;
	}

	return crc;
}

static uint32_t crc32c_intel_le_hw(uint32_t crc, unsigned char const *p, size_t len)
{
	unsigned int iquotient = len / SCALE_F;
	unsigned int iremainder = len % SCALE_F;
	unsigned long *ptmp = (unsigned long *)p;

	while (iquotient--) {
		asm(CRC32_INST
		    : "+r" (crc) : "rm" (*ptmp));
		ptmp++;
	}

	if (iremainder)
		crc = crc32c_intel_le_hw_byte(crc, (unsigned char *)ptmp,
				 iremainder);

	return crc;
}

int crc32c_intel_le(uint32_t *crc, unsigned char const *data, size_t length)
{
	if (!support_features)
		detect_features();
#ifdef __x86_64__
	if (support_features & HAVE_PCL) {
		*crc = crc_pcl(data, length, *crc);
		return 1;
	} else
#endif
	if (support_features & HAVE_SSE42) {
		*crc = crc32c_intel_le_hw(*crc, data, length);
		return 1;
	}
	return 0;
}
#endif
