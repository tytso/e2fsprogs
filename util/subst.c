/*
 * subst.c --- substitution program
 *
 * Subst is used as a quicky program to do @ substitutions
 * 
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif


struct subst_entry {
	char *name;
	char *value;
	struct subst_entry *next;
};

struct subst_entry *subst_table = 0;

static int add_subst(char *name, char *value)
{
	struct subst_entry	*ent = 0;
	int	retval;
	
	retval = ENOMEM;
	ent = malloc(sizeof(struct subst_entry));
	if (!ent)
		goto fail;
	ent->name = malloc(strlen(name)+1);
	if (!ent->name)
		goto fail;
	ent->value = malloc(strlen(value)+1);
	if (!ent->value)
		goto fail;
	strcpy(ent->name, name);
	strcpy(ent->value, value);
	ent->next = subst_table;
	subst_table = ent;
	return 0;
fail:
	if (ent) {
		if (ent->name)
			free(ent->name);
		if (ent->value)
			free(ent->value);
		free(ent);
	}
	return retval;
}

static struct subst_entry *fetch_subst_entry(char *name)
{
	struct subst_entry *ent;

	for (ent = subst_table; ent; ent = ent->next) {
		if (strcmp(name, ent->name) == 0)
			break;
	}
	return ent;
}

static void substitute_line(char *line)
{
	char	*ptr, *name_ptr, *end_ptr;
	struct subst_entry *ent;
	char	replace_name[128];
	int	len, replace_len;

	ptr = line;
	while (ptr) {
		name_ptr = strchr(ptr, '@');
		if (!name_ptr)
			break;
		end_ptr = strchr(name_ptr+1, '@');
		if (!end_ptr)
			break;
		len = end_ptr - name_ptr - 1;
		memcpy(replace_name, name_ptr+1, len);
		replace_name[len] = 0;
		ent = fetch_subst_entry(replace_name);
		if (!ent) {
			fprintf(stderr, "Unfound expansion: '%s'\n", 
				replace_name);
			ptr = end_ptr + 1;
			continue;
		}
#if 0
		fprintf(stderr, "Replace name = '%s' with '%s'\n",
		       replace_name, ent->value);
#endif
		replace_len = strlen(ent->value);
		if (replace_len != len+2)
			memmove(end_ptr+(replace_len-len-2), end_ptr,
				strlen(end_ptr)+1);
		memcpy(name_ptr, ent->value, replace_len);
		ptr = name_ptr;
	}
}

static void parse_config_file(FILE *f)
{
	char	line[2048];
	char	*cp, *ptr;

	while (!feof(f)) {
		memset(line, 0, sizeof(line));
		if (fgets(line, sizeof(line), f) == NULL)
			break;
		/*
		 * Strip newlines and comments.
		 */
		cp = strchr(line, '\n');
		if (cp)
			*cp = 0;
		cp = strchr(line, '#');
		if (cp)
			*cp = 0;
		/*
		 * Skip trailing and leading whitespace
		 */
		for (cp = line + strlen(line) - 1; cp >= line; cp--) {
			if (*cp == ' ' || *cp == '\t')
				*cp = 0;
			else
				break;
		}
		cp = line;
		while (*cp && isspace(*cp))
			cp++;
		ptr = cp;
		/*
		 * Skip empty lines
		 */
		if (*ptr == 0)
			continue;
		/*
		 * Ignore future extensions
		 */
		if (*ptr == '$')
			continue;
		/*
		 * Parse substitutions
		 */
		for (cp = ptr; *cp; cp++)
			if (isspace(*cp))
				break;
		*cp = 0;
		for (cp++; *cp; cp++)
			if (!isspace(*cp))
				break;
#if 0
		printf("Substitute: '%s' for '%s'\n", ptr, cp);
#endif
		add_subst(ptr, cp);
	}
}

/*
 * Return 0 if the files are different, 1 if the files are the same.
 */
static int compare_file(const char *outfn, const char *newfn)
{
	FILE	*old, *new;
	char	oldbuf[2048], newbuf[2048], *oldcp, *newcp;
	int	retval;

	old = fopen(outfn, "r");
	if (!old)
		return 0;
	new = fopen(newfn, "r");
	if (!new)
		return 0;

	while (1) {
		oldcp = fgets(oldbuf, sizeof(oldbuf), old);
		newcp = fgets(newbuf, sizeof(newbuf), new);
		if (!oldcp && !newcp) {
			retval = 1;
			break;
		}
		if (!oldcp || !newcp || strcmp(oldbuf, newbuf)) {
			retval = 0;
			break;
		}
	}
	return retval;
}

	


int main(int argc, char **argv)
{
	char	line[2048];
	int	c;
	FILE	*in, *out;
	char	*outfn = NULL, *newfn = NULL;
	int	verbose = 0;
	
	while ((c = getopt (argc, argv, "f:v")) != EOF) {
		switch (c) {
		case 'f':
			in = fopen(optarg, "r");
			if (!in) {
				perror(optarg);
				exit(1);
			}
			parse_config_file(in);
			fclose(in);
			break;
		case 'v':
			verbose++;
			break;
		default:
			fprintf(stderr, "%s: [-f config-file] [file]\n", 
				argv[0]);
			break;
		}
	}
	if (optind < argc) {
		in = fopen(argv[optind], "r");
		if (!in) {
			perror(argv[optind]);
			exit(1);
		}
		optind++;
	} else
		in = stdin;
	
	if (optind < argc) {
		outfn = argv[optind];
		newfn = malloc(strlen(outfn)+20);
		if (!newfn) {
			fprintf(stderr, "Memory error!  Exiting.\n");
			exit(1);
		}
		strcpy(newfn, outfn);
		strcat(newfn, ".new");
		out = fopen(newfn, "w");
		if (!out) {
			perror(newfn);
			exit(1);
		}
	} else {
		out = stdout;
		outfn = 0;
	}
			
	while (!feof(in)) {
		if (fgets(line, sizeof(line), in) == NULL)
			break;
		substitute_line(line);
		fputs(line, out);
	}
	fclose(in);
	fclose(out);
	if (outfn) {
		if (compare_file(outfn, newfn)) {
			if (verbose)
				printf("No change, keeping %s.\n", outfn);
			unlink(newfn);
		} else {
			if (verbose)
				printf("Creating or replacing %s.\n", outfn);
			rename(newfn, outfn);
		}
	}
	return (0);
}


