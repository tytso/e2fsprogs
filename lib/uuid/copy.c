/*
 * copy.c --- copy UUIDs
 */

#include "uuidP.h"

void uuid_copy(uuid_t uu1, uuid_t uu2)
{
	unsigned char 	*cp1, *cp2;
	int		i;

	for (i=0, cp1 = uu1, cp2 = uu2; i < 16; i++)
		*cp1++ = *cp2++;
}
