#ifdef ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#define _(a) (gettext (a))
#ifdef gettext_noop
#define N_(a) gettext_noop (a)
#else
#define N_(a) (a)
#endif
/* FIXME */
#define NLS_CAT_NAME "e2fsprogs"
#define LOCALEDIR "/usr/share/locale"
/* FIXME */
#else
#define _(a) (a)
#define N_(a) a
#endif
