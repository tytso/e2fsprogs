#ifndef _EXT2FS_COMPILER_H
#define _EXT2FS_COMPILER_H

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#undef offsetof
#if __has_builtin(__builtin_offsetof)
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#elif defined(__compiler_offsetof)
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({			\
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})


#endif /* _EXT2FS_COMPILER_H */
