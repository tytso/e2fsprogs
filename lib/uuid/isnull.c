/*
 * isnull.c --- Check whether or not the UUID is null
 */

#include "uuidP.h"

/* Returns 1 if the uuid is the NULL uuid */
int uuid_is_null(uuid_t uu)
{
	unsigned char 	*cp;
	int		i;

	for (i=0, cp = uu; i < 16; i++)
		if (*cp++)
			return 0;
	return 1;
}

