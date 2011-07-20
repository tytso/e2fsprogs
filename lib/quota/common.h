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

#ifdef ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#define _(a) (gettext (a))
#ifdef gettext_noop
#define N_(a) gettext_noop (a)
#else
#define N_(a) (a)
#endif
#define P_(singular, plural, n) (ngettext (singular, plural, n))
#ifndef NLS_CAT_NAME
#define NLS_CAT_NAME "e2fsprogs"
#endif
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif
#else
#define _(a) (a)
#define N_(a) a
#define P_(singular, plural, n) ((n) == 1 ? (singular) : (plural))
#endif

#define log_fatal(exit_code, format, ...)	do { \
		fprintf(stderr, _("[FATAL] %s:%d:%s:: " format "\n"), \
			__FILE__, __LINE__, __func__, __VA_ARGS__); \
		exit(exit_code); \
	} while (0)

#define log_err(format, ...)	fprintf(stderr, \
				_("[ERROR] %s:%d:%s:: " format "\n"), \
				__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef DEBUG_QUOTA
# define log_debug(format, ...)	fprintf(stderr, \
				_("[DEBUG] %s:%d:%s:: " format "\n"), \
				__FILE__, __LINE__, __func__, __VA_ARGS__)
#else
# define log_debug(format, ...)
#endif

#define BUG_ON(x)		do { if ((x)) { \
					fprintf(stderr, \
						_("BUG_ON: %s:%d:: ##x"), \
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
