/*
 * gen_uuid.c --- generate a DCE-compatible uuid
 *
 * Copyright (C) 1999, Andreas Dilger
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <stdio.h>
#include "uuid/uuid.h"

int
main (int argc, char *argv[])
{
   char   str[37];
   uuid_t uu;

   uuid_generate(uu);
   uuid_unparse(uu, str);

   printf("%s\n", str);

   return 0;
}
