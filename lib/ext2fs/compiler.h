#ifndef _EXT2FS_COMPILER_H
#define _EXT2FS_COMPILER_H

#include <stddef.h>

#ifdef __GNUC__

#define container_of(ptr, type, member) ({				\
	__typeof__( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#else
#define container_of(ptr, type, member)				\
	((type *)((char *)(ptr) - offsetof(type, member)))
#endif


#endif /* _EXT2FS_COMPILER_H */
