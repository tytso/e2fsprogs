/*
 *
 *	Various things common for all utilities
 *
 */

#ifndef __QUOTA_COMMON_H__
#define __QUOTA_COMMON_H__

#ifndef __attribute__
# if !defined __GNUC__ || __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 8) || __STRICT_ANSI__
#  define __attribute__(x)
# endif
#endif

#define log_fatal(exit_code, format, ...)	do { \
		fprintf(stderr, "[FATAL] %s:%d:%s:: " format "\n", \
			__FILE__, __LINE__, __func__, __VA_ARGS__); \
		exit(exit_code); \
	} while (0)

#define log_err(format, ...)	fprintf(stderr, \
				"[ERROR] %s:%d:%s:: " format "\n", \
				__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef DEBUG_QUOTA
# define log_debug(format, ...)	fprintf(stderr, \
				"[DEBUG] %s:%d:%s:: " format "\n", \
				__FILE__, __LINE__, __func__, __VA_ARGS__)
#else
# define log_debug(format, ...)
#endif

#define BUG_ON(x)		do { if ((x)) { \
					fprintf(stderr, \
						"BUG_ON: %s:%d:: ##x", \
						__FILE__, __LINE__); \
					exit(2); \
				} } while (0)

/* malloc() with error check */
void *smalloc(size_t);

/* realloc() with error check */
void *srealloc(void *, size_t);

/* Safe strncpy - always finishes string */
void sstrncpy(char *, const char *, size_t);

/* Safe strncat - always finishes string */
void sstrncat(char *, const char *, size_t);

/* Safe version of strdup() */
char *sstrdup(const char *s);

#endif /* __QUOTA_COMMON_H__ */
