/*
 * pfsck --- A generic, parallelizing front-end for the fsck program.
 * It will automatically try to run fsck programs in parallel if the
 * devices are on separate spindles.  It is based on the same ideas as
 * the generic front end for fsck by David Engel and Fred van Kempen,
 * but it has been completely rewritten from scratch to support
 * parallel execution.
 *
 * Written by Theodore Ts'o, <tytso@mit.edu>
 * 
 * Usage:	fsck [-ACVRNTM] [-s] [-t fstype] [fs-options] device
 * 
 * Miquel van Smoorenburg (miquels@drinkel.ow.org) 20-Oct-1994:
 *   o Changed -t fstype to behave like with mount when -A (all file
 *     systems) or -M (like mount) is specified.
 *   o fsck looks if it can find the fsck.type program to decide
 *     if it should ignore the fs type. This way more fsck programs
 *     can be added without changing this front-end.
 *   o -R flag skip root file system.
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_PATHS_H
#include <paths.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <malloc.h>

#include "../version.h"
#include "fsck.h"
#include "get_device_by_label.h"

#ifndef _PATH_MNTTAB
#define	_PATH_MNTTAB	"/etc/fstab"
#endif

static const char *ignored_types[] = {
	"ignore",
	"iso9660",
	"nfs",
	"proc",
	"sw",
	"swap",
	NULL
};

static const char *really_wanted[] = {
	"minix",
	"ext2",
	"xiafs",
	NULL
};

#ifdef DEV_DSK_DEVICES
static const char *base_devices[] = {
	"/dev/dsk/hda",
	"/dev/dsk/hdb",
	"/dev/dsk/hdc",
	"/dev/dsk/hdd",
	"/dev/dsk/hd1a",
	"/dev/dsk/hd1b",
	"/dev/dsk/hd1c",
	"/dev/dsk/hd1d",
	"/dev/dsk/sda",
	"/dev/dsk/sdb",
	"/dev/dsk/sdc",
	"/dev/dsk/sdd",
	"/dev/dsk/sde",
	"/dev/dsk/sdf",
	"/dev/dsk/sdg",
	NULL
};
#else
static const char *base_devices[] = {
	"/dev/hda",
	"/dev/hdb",
	"/dev/hdc",
	"/dev/hdd",
	"/dev/hd1a",
	"/dev/hd1b",
	"/dev/hd1c",
	"/dev/hd1d",
	"/dev/sda",
	"/dev/sdb",
	"/dev/sdc",
	"/dev/sdd",
	"/dev/sde",
	"/dev/sdf",
	"/dev/sdg",
	NULL
};
#endif

/*
 * Global variables for options
 */
char *devices[MAX_DEVICES];
char *args[MAX_ARGS];
int num_devices, num_args;

int verbose = 0;
int doall = 0;
int noexecute = 0;
int serialize = 0;
int skip_root = 0;
int like_mount = 0;
int notitle = 0;
int parallel_root = 0;
int progress = 0;
int force_all_parallel = 0;
char *progname;
char *fstype = NULL;
struct fs_info *filesys_info;
struct fsck_instance *instance_list;
const char *fsck_prefix_path = "/sbin:/sbin/fs.d:/sbin/fs:/etc/fs:/etc";
char *fsck_path = 0;
static int ignore(struct fs_info *);

static char *string_copy(const char *s)
{
	char	*ret;

	ret = malloc(strlen(s)+1);
	if (ret)
		strcpy(ret, s);
	return ret;
}

static char *skip_over_blank(char *cp)
{
	while (*cp && isspace(*cp))
		cp++;
	return cp;
}

static char *skip_over_word(char *cp)
{
	while (*cp && !isspace(*cp))
		cp++;
	return cp;
}

static void strip_line(char *line)
{
	char	*p;

	while (*line) {
		p = line + strlen(line) - 1;
		if ((*p == '\n') || (*p == '\r'))
			*p = 0;
		else
			break;
	}
}

static char *parse_word(char **buf)
{
	char *word, *next;

	word = *buf;
	if (*word == 0)
		return 0;

	word = skip_over_blank(word);
	next = skip_over_word(word);
	if (*next)
		*next++ = 0;
	*buf = next;
	return word;
}

static void free_instance(struct fsck_instance *i)
{
	if (i->prog)
		free(i->prog);
	if (i->device)
		free(i->device);
	free(i);
	return;
}

static int parse_fstab_line(char *line, struct fs_info **ret_fs)
{
	char	*device, *mntpnt, *type, *opts, *freq, *passno, *cp;
	struct fs_info *fs;

	*ret_fs = 0;
	strip_line(line);
	if ((cp = strchr(line, '#')))
		*cp = 0;	/* Ignore everything after the comment char */
	cp = line;

	device = parse_word(&cp);
	mntpnt = parse_word(&cp);
	type = parse_word(&cp);
	opts = parse_word(&cp);
	freq = parse_word(&cp);
	passno = parse_word(&cp);

	if (!device)
		return 0;	/* Allow blank lines */
	
	if (!mntpnt || !type)
		return -1;
	
	if (!(fs = malloc(sizeof(struct fs_info))))
		return -1;

	fs->device = string_copy(device);
	fs->mountpt = string_copy(mntpnt);
	fs->type = string_copy(type);
	fs->opts = string_copy(opts ? opts : "");
	fs->freq = freq ? atoi(freq) : -1;
	fs->passno = passno ? atoi(passno) : -1;
	fs->flags = 0;
	fs->next = NULL;

	*ret_fs = fs;

	return 0;
}

/*
 * Interpret the device name if necessary 
 */
static char *interpret_device(char *spec)
{
	char *dev = NULL;
	
	if (!strncmp(spec, "UUID=", 5))
		dev = get_spec_by_uuid(spec+5);
	else if (!strncmp(spec, "LABEL=", 6))
		dev = get_spec_by_volume_label(spec+6);
	if (dev) {
		free(spec);
		spec = dev;
	}
	return spec;
}

/*
 * Load the filesystem database from /etc/fstab
 */
static void load_fs_info(const char *filename)
{
	FILE	*f;
	char	buf[1024];
	int	lineno = 0;
	int	old_fstab = 1;
	struct fs_info *fs, *fs_last = NULL;

	filesys_info = NULL;
	if ((f = fopen(filename, "r")) == NULL) {
		fprintf(stderr, "WARNING: couldn't open %s: %s\n",
			filename, strerror(errno));
		return;
	}
	while (!feof(f)) {
		lineno++;
		if (!fgets(buf, sizeof(buf), f))
			break;
		buf[sizeof(buf)-1] = 0;
		if (parse_fstab_line(buf, &fs) < 0) {
			fprintf(stderr, "WARNING: bad format "
				"on line %d of %s\n", lineno, filename);
			continue;
		}
		if (!fs)
			continue;
		fs->device = interpret_device(fs->device);
		if (!filesys_info)
			filesys_info = fs;
		else
			fs_last->next = fs;
		fs_last = fs;
		if (fs->passno < 0)
			fs->passno = 0;
		else
			old_fstab = 0;
	}
	
	fclose(f);
	
	if (old_fstab) {
		fprintf(stderr, "\007\007\007"
	"WARNING: Your /etc/fstab does not contain the fsck passno\n");
		fprintf(stderr,
	"	field.  I will kludge around things for you, but you\n");
		fprintf(stderr,
	"	should fix your /etc/fstab file as soon as you can.\n\n");
		
		for (fs = filesys_info; fs; fs = fs->next) {
			fs->passno = 1;
		}
	}
}
	
/* Lookup filesys in /etc/fstab and return the corresponding entry. */
static struct fs_info *lookup(char *filesys)
{
	struct fs_info *fs;

	/* No filesys name given. */
	if (filesys == NULL)
		return NULL;

	for (fs = filesys_info; fs; fs = fs->next) {
		if (!strcmp(filesys, fs->device) ||
		    !strcmp(filesys, fs->mountpt))
			break;
	}

	return fs;
}

/* Find fsck program for a given fs type. */
static char *find_fsck(char *type)
{
  char *s;
  const char *tpl;
  static char prog[256];
  char *p = string_copy(fsck_path);
  struct stat st;

  /* Are we looking for a program or just a type? */
  tpl = (strncmp(type, "fsck.", 5) ? "%s/fsck.%s" : "%s/%s");

  for(s = strtok(p, ":"); s; s = strtok(NULL, ":")) {
	sprintf(prog, tpl, s, type);
	if (stat(prog, &st) == 0) break;
  }
  free(p);
  return(s ? prog : NULL);
}

static int progress_active(NOARGS)
{
	struct fsck_instance *inst;

	for (inst = instance_list; inst; inst = inst->next) {
		if (inst->flags & FLAG_DONE)
			continue;
		if (inst->flags & FLAG_PROGRESS)
			return 1;
	}
	return 0;
}

/*
 * Execute a particular fsck program, and link it into the list of
 * child processes we are waiting for.
 */
static int execute(const char *type, char *device, char *mntpt,
		   int interactive)
{
	char *s, *argv[80], prog[80];
	int  argc, i;
	struct fsck_instance *inst, *p;
	pid_t	pid;

	inst = malloc(sizeof(struct fsck_instance));
	if (!inst)
		return ENOMEM;
	memset(inst, 0, sizeof(struct fsck_instance));

	sprintf(prog, "fsck.%s", type);
	argv[0] = string_copy(prog);
	argc = 1;
	
	for (i=0; i <num_args; i++)
		argv[argc++] = string_copy(args[i]);

	if (progress & !progress_active()) {
		if (strcmp(type, "ext2") == 0) {
			argv[argc++] = string_copy("-C0");
			inst->flags |= FLAG_PROGRESS;
		}
	}

	argv[argc++] = string_copy(device);
	argv[argc] = 0;

	s = find_fsck(prog);
	if (s == NULL) {
		fprintf(stderr, "fsck: %s: not found\n", prog);
		return ENOENT;
	}

	if (verbose || noexecute) {
		printf("[%s -- %s] ", s, mntpt ? mntpt : device);
		for (i=0; i < argc; i++)
			printf("%s ", argv[i]);
		printf("\n");
	}
	
	/* Fork and execute the correct program. */
	if (noexecute)
		pid = -1;
	else if ((pid = fork()) < 0) {
		perror("fork");
		return errno;
	} else if (pid == 0) {
		if (!interactive)
			close(0);
		(void) execv(s, argv);
		perror(argv[0]);
		exit(EXIT_ERROR);
	}

	for (i=0; i < argc; i++)
		free(argv[i]);
	
	inst->pid = pid;
	inst->prog = string_copy(prog);
	inst->type = string_copy(type);
	inst->device = string_copy(device);
	inst->start_time = time(0);
	inst->next = NULL;

	/*
	 * Find the end of the list, so we add the instance on at the end.
	 */
	for (p = instance_list; p && p->next; p = p->next);

	if (p)
		p->next = inst;
	else
		instance_list = inst;
	
	return 0;
}

/*
 * Wait for one child process to exit; when it does, unlink it from
 * the list of executing child processes, and return it.
 */
static struct fsck_instance *wait_one(NOARGS)
{
	int	status;
	int	sig;
	struct fsck_instance *inst, *inst2, *prev;
	pid_t	pid;

	if (!instance_list)
		return NULL;

	if (noexecute) {
		inst = instance_list;
		instance_list = inst->next;
		inst->exit_status = 0;
		return(inst);
	}

	/*
	 * gcc -Wall fails saving throw against stupidity
	 * (inst and prev are thought to be uninitialized variables)
	 */
	inst = prev = NULL;
	
	do {
		pid = wait(&status);
		if (pid < 0) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			if (errno == ECHILD) {
				fprintf(stderr,
					"%s: wait: No more child process?!?\n",
					progname);
				return NULL;
			}
			perror("wait");
			continue;
		}
		for (prev = 0, inst = instance_list;
		     inst;
		     prev = inst, inst = inst->next) {
			if (inst->pid == pid)
				break;
		}
	} while (!inst);

	if (WIFEXITED(status)) 
		status = WEXITSTATUS(status);
	else if (WIFSIGNALED(status)) {
		sig = WTERMSIG(status);
		if (sig == SIGINT) {
			status = EXIT_UNCORRECTED;
		} else {
			printf("Warning... %s for device %s exited "
			       "with signal %d.\n",
			       inst->prog, inst->device, sig);
			status = EXIT_ERROR;
		}
	} else {
		printf("%s %s: status is %x, should never happen.\n",
		       inst->prog, inst->device, status);
		status = EXIT_ERROR;
	}
	inst->exit_status = status;
	if (prev)
		prev->next = inst->next;
	else
		instance_list = inst->next;
	if (progress && (inst->flags & FLAG_PROGRESS) &&
	    !progress_active()) {
		for (inst2 = instance_list; inst2; inst2 = inst2->next) {
			if (inst2->flags & FLAG_DONE)
				continue;
			if (strcmp(inst2->type, "ext2"))
				continue;
			/*
			 * If we've just started the fsck, wait a tiny
			 * bit before sending the kill, to give it
			 * time to set up the signal handler
			 */
			if (inst2->start_time < time(0)+2) {
				if (fork() == 0) {
					sleep(1);
					kill(inst2->pid, SIGUSR1);
					exit(0);
				}
			} else
				kill(inst2->pid, SIGUSR1);
			break;
		}
	}
	return inst;
}

/*
 * Wait until all executing child processes have exited; return the
 * logical OR of all of their exit code values.
 */
static int wait_all(NOARGS)
{
	struct fsck_instance *inst;
	int	global_status = 0;

	while (instance_list) {
		inst = wait_one();
		if (!inst)
			break;
		global_status |= inst->exit_status;
		free_instance(inst);
	}
	return global_status;
}

/*
 * Run the fsck program on a particular device
 * 
 * If the type is specified using -t, and it isn't prefixed with "no"
 * (as in "noext2") and only one filesystem type is specified, then
 * use that type regardless of what is specified in /etc/fstab.
 * 
 * If the type isn't specified by the user, then use either the type
 * specified in /etc/fstab, or DEFAULT_FSTYPE.
 */
static void fsck_device(char *device, int interactive)
{
	const char *type = 0;
	struct fs_info *fsent;
	int retval;

	if (fstype && strncmp(fstype, "no", 2) && !strchr(fstype, ','))
		type = fstype;

	if ((fsent = lookup(device))) {
		device = fsent->device;
		if (!type)
			type = fsent->type;
	}
	if (!type)
		type = DEFAULT_FSTYPE;

	retval = execute(type, device, fsent ? fsent->mountpt : 0,
			 interactive);
	if (retval) {
		fprintf(stderr, "%s: Error %d while executing fsck.%s "
			"for %s\n", progname, retval, type, device);
	}
}

/* See if filesystem type matches the list. */
static int fs_match(char *type, char *fs_type)
{
  int ret = 0, negate = 0;
  char list[128];
  char *s;

  if (!fs_type) return(1);

  if (strncmp(fs_type, "no", 2) == 0) {
	fs_type += 2;
	negate = 1;
  }
  strcpy(list, fs_type);
  s = strtok(list, ",");
  while(s) {
	if (strcmp(s, type) == 0) {
		ret = 1;
		break;
	}
	s = strtok(NULL, ",");
  }
  return(negate ? !ret : ret);
}


/* Check if we should ignore this filesystem. */
static int ignore(struct fs_info *fs)
{
	const char **ip;
	int wanted = 0;

	/*
	 * If the pass number is 0, ignore it.
	 */
	if (fs->passno == 0)
		return 1;

	/*
	 * If a specific fstype is specified, and it doesn't match,
	 * ignore it.
	 */
	if (!fs_match(fs->type, fstype)) return 1;
	
	/* Are we ignoring this type? */
	for(ip = ignored_types; *ip; ip++)
		if (strcmp(fs->type, *ip) == 0) return(1);

	/* Do we really really want to check this fs? */
	for(ip = really_wanted; *ip; ip++)
		if (strcmp(fs->type, *ip) == 0) {
			wanted = 1;
			break;
		}

	/* See if the <fsck.fs> program is available. */
	if (find_fsck(fs->type) == NULL) {
		if (wanted)
			fprintf(stderr, "fsck: cannot check %s: fsck.%s not found\n",
				fs->device, fs->type);
		return(1);
	}

	/* We can and want to check this file system type. */
	return 0;
}

/*
 * Return the "base device" given a particular device; this is used to
 * assure that we only fsck one partition on a particular drive at any
 * one time.  Otherwise, the disk heads will be seeking all over the
 * place.
 */
static const char *base_device(char *device)
{
	const char **base;

	for (base = base_devices; *base; base++) {
		if (!strncmp(*base, device, strlen(*base)))
			return *base;
	}
	return device;
}

/*
 * Returns TRUE if a partition on the same disk is already being
 * checked.
 */
static int device_already_active(char *device)
{
	struct fsck_instance *inst;
	const char *base = base_device(device);

	if (force_all_parallel)
		return 0;

	for (inst = instance_list; inst; inst = inst->next) {
		if (!strcmp(base, base_device(inst->device)))
			return 1;
	}
	return 0;
}

/* Check all file systems, using the /etc/fstab table. */
static int check_all(NOARGS)
{
	struct fs_info *fs = NULL;
	struct fsck_instance *inst;
	int status = EXIT_OK;
	int not_done_yet = 1;
	int passno = 1;
	int pass_done;

	if (verbose)
		printf("Checking all file systems.\n");

	/*
	 * Find and check the root filesystem first.
	 */
	if (!parallel_root) {
		for (fs = filesys_info; fs; fs = fs->next) {
			if (!strcmp(fs->mountpt, "/"))
				break;
		}
		if (fs && !skip_root && !ignore(fs)) {
			fsck_device(fs->device, 0);
			fs->flags |= FLAG_DONE;
			status |= wait_all();
			if (status > EXIT_NONDESTRUCT)
				return status;
		}
	}
	if (fs) fs->flags |= FLAG_DONE;

	/*
	 * Mark filesystems that should be ignored as done.
	 */
	for (fs = filesys_info; fs; fs = fs->next) {
		if (ignore(fs))
			fs->flags |= FLAG_DONE;
	}
		
	while (not_done_yet) {
		not_done_yet = 0;
		pass_done = 1;

		for (fs = filesys_info; fs; fs = fs->next) {
			if (fs->flags & FLAG_DONE)
				continue;
			/*
			 * If the filesystem's pass number is higher
			 * than the current pass number, then we don't
			 * do it yet.
			 */
			if (fs->passno > passno) {
				not_done_yet++;
				continue;
			}
			/*
			 * If a filesystem on a particular device has
			 * already been spawned, then we need to defer
			 * this to another pass.
			 */
			if (device_already_active(fs->device)) {
				pass_done = 0;
				continue;
			}
			/*
			 * Spawn off the fsck process
			 */
			fsck_device(fs->device, 0);
			fs->flags |= FLAG_DONE;

			if (serialize) {
				pass_done = 0;
				break; /* Only do one filesystem at a time */

			}
		}
		if (verbose > 1)
			printf("--waiting-- (pass %d)\n", passno);
		inst = wait_one();
		if (inst) {
			status |= inst->exit_status;
			free_instance(inst);
		}
		if (pass_done) {
			status |= wait_all();
			if (verbose > 1) 
				printf("----------------------------------\n");
			passno++;
		} else
			not_done_yet++;
	}
	status |= wait_all();
	return status;
}

static void usage(NOARGS)
{
	fprintf(stderr,
		"Usage: fsck [-ACNPRTV] [-t fstype] [fs-options] filesys\n");
	exit(EXIT_USAGE);
}

static void PRS(int argc, char *argv[])
{
	int	i, j;
	char	*arg;
	char	options[128];
	int	opt = 0;
	int     opts_for_fsck = 0;
	
	num_devices = 0;
	num_args = 0;
	instance_list = 0;

	progname = argv[0];

	for (i=1; i < argc; i++) {
		arg = argv[i];
		if (!arg)
			continue;
		if ((arg[0] == '/' && !opts_for_fsck) ||
		    (strncmp(arg, "LABEL=", 6) == 0) ||
		    (strncmp(arg, "UUID=", 5) == 0)) {
			if (num_devices >= MAX_DEVICES) {
				fprintf(stderr, "%s: too many devices\n",
					progname);
				exit(1);
			}
			devices[num_devices++] =
				interpret_device(string_copy(arg));
			continue;
		}
		if (arg[0] != '-' || opts_for_fsck) {
			if (num_args >= MAX_ARGS) {
				fprintf(stderr, "%s: too many arguments\n",
					progname);
				exit(1);
			}
			args[num_args++] = string_copy(arg);
			continue;
		}
		for (j=1; arg[j]; j++) {
			if (opts_for_fsck) {
				options[++opt] = arg[j];
				continue;
			}
			switch (arg[j]) {
			case 'A':
				doall++;
				break;
			case 'C':
				progress++;
				break;
			case 'V':
				verbose++;
				break;
			case 'N':
				noexecute++;
				break;
			case 'R':
				skip_root++;
				break;
			case 'T':
				notitle++;
				break;
			case 'M':
				like_mount++;
				break;
			case 'P':
				parallel_root++;
				break;
			case 's':
				serialize++;
				break;
			case 't':
				if (arg[j+1]) {
					fstype = string_copy(arg+j+1);
					goto next_arg;
				}
				if ((i+1) < argc) {
					i++;
					fstype = string_copy(argv[i]);
					goto next_arg;
				}
				usage();
				break;
			case '-':
				opts_for_fsck++;
				break;
			default:
				options[++opt] = arg[j];
				break;
			}
		}
	next_arg:
		if (opt) {
			options[0] = '-';
			options[++opt] = '\0';
			if (num_args >= MAX_ARGS) {
				fprintf(stderr,
					"%s: too many arguments\n",
					progname);
				exit(1);
			}
			args[num_args++] = string_copy(options);
			opt = 0;
		}
	}
	if (getenv("FSCK_FORCE_ALL_PARALLEL"))
		force_all_parallel++;
}

int main(int argc, char *argv[])
{
	int i;
	int status = 0;
	int interactive = 0;
	char *oldpath = getenv("PATH");
	const char *fstab;

	PRS(argc, argv);

	if (!notitle)
		printf("Parallelizing fsck version %s (%s)\n",
			E2FSPROGS_VERSION, E2FSPROGS_DATE);

	fstab = getenv("FSTAB_FILE");
	if (!fstab)
		fstab = _PATH_MNTTAB;
	load_fs_info(fstab);

	/* Update our search path to include uncommon directories. */
	if (oldpath) {
		fsck_path = malloc (strlen (fsck_prefix_path) + 1 +
				    strlen (oldpath) + 1);
		strcpy (fsck_path, fsck_prefix_path);
		strcat (fsck_path, ":");
		strcat (fsck_path, oldpath);
	} else {
		fsck_path = string_copy(fsck_prefix_path);
	}
	
	if ((num_devices == 1) || (serialize))
		interactive = 1;

	/* If -A was specified ("check all"), do that! */
	if (doall)
		return check_all();

	for (i = 0 ; i < num_devices; i++) {
		fsck_device(devices[i], interactive);
		if (serialize) {
			struct fsck_instance *inst;

			inst = wait_one();
			if (inst) {
				status |= inst->exit_status;
				free_instance(inst);
			}
		}
	}
	status |= wait_all();
	free(fsck_path);
	return status;
}
