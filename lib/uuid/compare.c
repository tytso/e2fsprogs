/*
 * compare.c --- compare whether or not two UUID's are the same
 *
 * Returns 0 if the two UUID's are different, and 1 if they are the same.
 */

#include "uuidP.h"

int uuid_compare(uuid_t uu1, uuid_t uu2)
{
	unsigned char 	*cp1, *cp2;
	int		i;

	for (i=0, cp1 = uu1, cp2 = uu2; i < 16; i++)
		if (*cp1++ != *cp2++)
			return 0;
	return 1;
}

