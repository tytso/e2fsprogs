/*
 * Public include file for the UUID library
 */

typedef unsigned char uuid_t[16];

/* clear.c */
void uuid_clear(uuid_t uu);

/* compare.c */
int uuid_compare(uuid_t uu1, uuid_t uu2);

/* copy.c */
void uuid_copy(uuid_t uu1, uuid_t uu2);

/* gen_uuid.c */
void uuid_generate(uuid_t out);

/* isnull.c */
int uuid_is_null(uuid_t uu);

/* parse.c */
int uuid_parse(char *in, uuid_t uu);

/* unparse.c */
void uuid_unparse(uuid_t uu, char *out);



