#include "nls.h"

#include <errno.h>
#include <string.h>


static unsigned char charset_tolower(const struct nls_table *table,
				     unsigned int c)
{
	if (c >= 'A' && c <= 'Z')
		return (c | 0x20);
	return c;
}

static unsigned char charset_toupper(const struct nls_table *table,
				     unsigned int c)
{
	if (c >= 'a' && c <= 'z')
		return (c & ~0x20);
	return c;
}

static int ascii_casefold(const struct nls_table *table,
			  const unsigned char *str, size_t len,
			  unsigned char *dest, size_t dlen)
{
	int i;

	if (dlen < len)
		return -ENAMETOOLONG;

	for (i = 0; i < len; i++) {
		if (str[i] & 0x80)
			return -EINVAL;

		dest[i] = charset_toupper(table, str[i]);
	}

	return len;
}

static int ascii_normalize(const struct nls_table *table,
			   const unsigned char *str, size_t len,
			   unsigned char *dest, size_t dlen)
{
	int i;

	if (dlen < len)
		return -ENAMETOOLONG;

	for (i = 0; i < len; i++) {
		if (str[i] & 0x80)
			return -EINVAL;

		dest[i] = str[i];
	}

	return len;
}

const static struct nls_ops ascii_ops = {
	.casefold = ascii_casefold,
	.normalize = ascii_normalize,
};

const struct nls_table nls_ascii = {
	.ops = &ascii_ops,
};
