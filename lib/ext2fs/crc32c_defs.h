/*
 * This is the CRC32c polynomial, as outlined by Castagnoli.
 * x^32+x^28+x^27+x^26+x^25+x^23+x^22+x^20+x^19+x^18+x^14+x^13+x^11+x^10+x^9+
 * x^8+x^6+x^0
 */
#define CRCPOLY_LE 0x82F63B78
#define CRCPOLY_BE 0x1EDC6F41

/* How many bits at a time to use.  Valid values are 1, 2, 4, 8, 32 and 64. */
/* For less performance-sensitive, use 4 */
#ifndef CRC_LE_BITS
# define CRC_LE_BITS 64
#endif
#ifndef CRC_BE_BITS
# define CRC_BE_BITS 64
#endif

/*
 * Little-endian CRC computation.  Used with serial bit streams sent
 * lsbit-first.  Be sure to use cpu_to_le32() to append the computed CRC.
 */
#if CRC_LE_BITS > 64 || CRC_LE_BITS < 1 || CRC_LE_BITS == 16 || \
	CRC_LE_BITS & CRC_LE_BITS-1
# error "CRC_LE_BITS must be one of {1, 2, 4, 8, 32, 64}"
#endif

/*
 * Big-endian CRC computation.  Used with serial bit streams sent
 * msbit-first.  Be sure to use cpu_to_be32() to append the computed CRC.
 */
#if CRC_BE_BITS > 64 || CRC_BE_BITS < 1 || CRC_BE_BITS == 16 || \
	CRC_BE_BITS & CRC_BE_BITS-1
# error "CRC_BE_BITS must be one of {1, 2, 4, 8, 32, 64}"
#endif


#define ___constant_swab32(x) \
	((uint32_t)( \
		(((uint32_t)(x) & (uint32_t)0x000000ffUL) << 24) | \
		(((uint32_t)(x) & (uint32_t)0x0000ff00UL) <<  8) | \
		(((uint32_t)(x) & (uint32_t)0x00ff0000UL) >>  8) | \
		(((uint32_t)(x) & (uint32_t)0xff000000UL) >> 24)))


#include "ext2fs.h"
#ifdef WORDS_BIGENDIAN
#define __constant_cpu_to_le32(x) ___constant_swab32((x))
#define __constant_cpu_to_be32(x) (x)
#define __be32_to_cpu(x) (x)
#define __cpu_to_be32(x) (x)
#define __cpu_to_le32(x) (ext2fs_cpu_to_le32((x)))
#define __le32_to_cpu(x) (ext2fs_le32_to_cpu((x)))
#else
#define __constant_cpu_to_le32(x) (x)
#define __constant_cpu_to_be32(x) ___constant_swab32((x))
#define __be32_to_cpu(x) (ext2fs_be32_to_cpu((x)))
#define __cpu_to_be32(x) (ext2fs_cpu_to_be32((x)))
#define __cpu_to_le32(x) (x)
#define __le32_to_cpu(x) (x)
#endif

#if (__GNUC__ >= 3)
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#else
#define likely(x)	(x)
#define unlikely(x)	(x)
#endif
