/*
 * uuid_to_str.c -- convert a UUID to string
 */

#include <stdio.h>

#include "uuidP.h"

void uuid_unparse(uuid_t uu, char *out)
{
	struct uuid uuid;

	uuid_unpack(uu, &uuid);
	sprintf(out,
		"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid.time_low, uuid.time_mid, uuid.time_hi_and_version,
		uuid.clock_seq >> 8, uuid.clock_seq & 0xFF,
		uuid.node[0], uuid.node[1], uuid.node[2],
		uuid.node[3], uuid.node[4], uuid.node[5]);
}

