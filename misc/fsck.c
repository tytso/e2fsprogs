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
 * Usage:	fsck [-AV] [-t fstype] [fs-options] device
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mntent.h>
#include <unistd.h>
#include <getopt.h>

#include "../version.h"
#include "fsck.h"

static const char *ignored_types[] = {
	"ignore",
	"iso9660",
	"msdos",
	"nfs",
	"proc",
	"sw",
	"swap",
	NULL
};

static const char *base_devices[] = {
	"/dev/hda",
	"/dev/hdb",
	"/dev/hdc",
	"/dev/hdd",
	"/dev/sda",
	"/dev/sdb",
	"/dev/sdc",
	"/dev/sdd",
	"/dev/sde",
	"/dev/sdf",
	"/dev/sdg",
	NULL
};

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
char *progname;
char *fstype = NULL;
struct fs_info *filesys_info;
struct fsck_instance *instance_list;

static char *strdup(char *s)
{
	char	*ret;

	ret = malloc(strlen(s)+1);
	if (ret)
		strcpy(ret, s);
	return ret;
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

/*
 * Load the filesystem database from /etc/fstab
 */
static void load_fs_info(NOARGS)
{
	FILE *mntfile;
	struct mntent *mp;
	struct fs_info *fs;

	filesys_info = NULL;
	
	/* Open the mount table. */
	if ((mntfile = setmntent(MNTTAB, "r")) == NULL) {
		perror(MNTTAB);
		exit(EXIT_ERROR);
	}

	while ((mp = getmntent(mntfile)) != NULL) {
		fs = malloc(sizeof(struct fs_info));
		memset(fs, 0, sizeof(struct fs_info));
		fs->device = strdup(mp->mnt_fsname);
		fs->mountpt = strdup(mp->mnt_dir);
		fs->type = strdup(mp->mnt_type);
		fs->opts = strdup(mp->mnt_opts);
		fs->freq = mp->mnt_freq;
		fs->passno = mp->mnt_passno;
		fs->next = filesys_info;
		filesys_info = fs;
	}

	(void) endmntent(mntfile);
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

/*
 * Execute a particular fsck program, and link it into the list of
 * child processes we are waiting for.
 */
static int execute(char *prog, char *device)
{
	char *argv[80];
	int  argc, i;
	struct fsck_instance *inst;
	pid_t	pid;

	argv[0] = strdup(prog);
	argc = 1;
	
	for (i=0; i <num_args; i++)
		argv[argc++] = strdup(args[i]);

	argv[argc++] = strdup(device);
	argv[argc] = 0;

	if (verbose || noexecute) {
		for (i=0; i < argc; i++)
			printf("%s ", argv[i]);
		printf("\n");
	}
	if (noexecute)
		return 0;
	
	/* Fork and execute the correct program. */
	if ((pid = fork()) < 0) {
		perror("fork");
		return errno;
	} else if (pid == 0) {
		(void) execvp(prog, argv);
		perror(args[0]);
		exit(EXIT_ERROR);
	}
	inst = malloc(sizeof(struct fsck_instance));
	if (!inst)
		return ENOMEM;
	memset(inst, 0, sizeof(struct fsck_instance));
	inst->pid = pid;
	inst->prog = strdup(prog);
	inst->device = strdup(device);
	inst->next = instance_list;
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
	struct fsck_instance *inst, *prev;
	pid_t	pid;

	if (!instance_list)
		return NULL;

retry:
	pid = wait(&status);
	status = WEXITSTATUS(status);
	if (pid < 0) {
		if ((errno == EINTR) || (errno == EAGAIN))
			goto retry;
		if (errno == ECHILD) {
			fprintf(stderr,
				"%s: wait: No more child process?!?\n",
				progname);
			return NULL;
		}
		perror("wait");
		goto retry;
	}
	for (prev = 0, inst = instance_list;
	     inst;
	     prev = inst, inst = inst->next) {
		if (inst->pid == pid)
			break;
	}
	if (!inst) {
		printf("Unexpected child process %d, status = 0x%x\n",
		       pid, status);
		goto retry;
	}
	
	inst->exit_status = status;
	if (prev)
		prev->next = inst->next;
	else
		instance_list = inst->next;
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
 */
static void fsck_device(char *device)
{
	const char	*type;
	struct fs_info *fsent;
	int retval;
	char prog[80];

	if (fstype)
		type = fstype;
	else if ((fsent = lookup(device))) {
		device = fsent->device;
		type = fsent->type;
	} else
		type = DEFAULT_FSTYPE;

	sprintf(prog, "fsck.%s", type);
	retval = execute(prog, device);
	if (retval) {
		fprintf(stderr, "%s: Error %d while executing %s for %s\n",
			progname, retval, prog, device);
	}
}

/* Check if we should ignore this filesystem. */
static int ignore(struct fs_info *fs)
{
	const char *cp;
	const char **ip;

	/*
	 * If a specific fstype is specified, and it doesn't match,
	 * ignore it.
	 */
	if (fstype && strcmp(fstype, fs->type))
		return 1;
	
	ip = ignored_types;
	while (*ip != NULL) {
		if (!strcmp(fs->type, *ip))
			return 1;
		ip++;
	}

	for (cp = strtok(fs->opts, ","); cp != NULL; cp = strtok(NULL, ",")) {
		if (!strcmp(cp, "noauto"))
			return 1;
	}

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
		base++;
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
	const char *base;

	base = base_device(device);

	for (inst = instance_list; inst; inst = inst->next) {
		if (!strcmp(base, base_device(inst->device)))
			return 1;
	}

	return 0;
}

/* Check all file systems, using the /etc/fstab table. */
static int check_all(NOARGS)
{
	struct fs_info *fs;
	struct fsck_instance *inst;
	int status = EXIT_OK;
	int not_done_yet = 1;
	int passno = 0;
	int pass_done;

	if (verbose)
		printf("Checking all file systems.\n");

	/*
	 * Find and check the root filesystem first.
	 */
	for (fs = filesys_info; fs; fs = fs->next) {
		if (!strcmp(fs->mountpt, "/"))
			break;
	}
	if (fs &&
	    (!fstype || !strcmp(fstype, fs->type))) {
		fsck_device(fs->device);
		fs->flags |= FLAG_DONE;
		status |= wait_all();
		if (status > EXIT_NONDESTRUCT)
			return status;
	}

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
			fsck_device(fs->device);
			fs->flags |= FLAG_DONE;

			if (serialize)
				break; /* Only do one filesystem at a time */
		}
		inst = wait_one();
		if (inst) {
			status |= inst->exit_status;
			free_instance(inst);
		}
		if (pass_done) {
			status |= wait_all();
			if (verbose) 
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
		"Usage: fsck [-AV] [-t fstype] [fs-options] filesys\n");
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

	load_fs_info();

	for (i=1; i < argc; i++) {
		arg = argv[i];
		if (!arg)
			continue;
		if (arg[0] == '/') {
			if (num_devices >= MAX_DEVICES) {
				fprintf(stderr, "%s: too many devices\n",
					progname);
				exit(1);
			}
			devices[num_devices++] = strdup(arg);
			continue;
		}
		if (arg[0] != '-') {
			if (num_args >= MAX_ARGS) {
				fprintf(stderr, "%s: too many arguments\n",
					progname);
				exit(1);
			}
			args[num_args++] = strdup(arg);
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
			case 'V':
				verbose++;
				break;
			case 'N':
				noexecute++;
				break;
			case 's':
				serialize++;
				break;
			case 't':
				if (arg[j+1]) {
					fstype = strdup(arg+j+1);
					goto next_arg;
				}
				if ((i+1) < argc) {
					i++;
					fstype = strdup(argv[i]);
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
			args[num_args++] = strdup(options);
			opt = 0;
		}
	}
}

int main(int argc, char *argv[])
{
	char *oldpath, newpath[PATH_MAX];
	int status = 0;
	int i;

	PRS(argc, argv);

	printf("Parallelizing fsck version %s (%s)\n", E2FSPROGS_VERSION,
	       E2FSPROGS_DATE);

	/* Update our PATH to include /sbin, /etc/fs, and /etc. */
	strcpy(newpath, "PATH=/sbin:/etc/fs:/etc:");
	if ((oldpath = getenv("PATH")) != NULL)
		strcat(newpath, oldpath);
	putenv(newpath);
    
	/* If -A was specified ("check all"), do that! */
	if (doall)
		return check_all();

	for (i = 0 ; i < num_devices; i++) {
		fsck_device(devices[i]);
		if (serialize) {
			struct fsck_instance *inst;

			inst = wait_one();
			if (!inst) {
				status |= inst->exit_status;
				free_instance(inst);
			}
		}
	}

	status |= wait_all();
	return status;
}
