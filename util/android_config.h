/* work around bug in AndroidConfig.h */
#ifdef HAVE_MALLOC_H
#undef HAVE_MALLOC_H
#define HAVE_MALLOC_H 1
#endif

#define ROOT_SYSCONFDIR "/etc"
