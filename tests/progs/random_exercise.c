/*
 * random_exercise.c --- Test program which exercises an ext2
 * 	filesystem.  It creates a lot of random files in the current
 * 	directory, while holding some files open while they are being
 * 	deleted.  This exercises the orphan list code, as well as
 * 	creating lots of fodder for the ext3 journal.
 * 
 * Copyright (C) 2000 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAXFDS	128

struct state {
	char	name[16];
	int	state;
};

#define STATE_CLEAR	0
#define STATE_CREATED	1
#define STATE_DELETED	2

struct state state_array[MAXFDS];

void clear_state_array()
{
	int	i;

	for (i = 0; i < MAXFDS; i++)
		state_array[i].state = STATE_CLEAR;
}

int get_random_fd()
{
	int	fd;

	while (1) {
		fd = ((int) random()) % MAXFDS;
		if (fd > 2)
			return fd;
	}
}

void create_random_file()
{
	char template[16] = "EX.XXXXXX";
	int	fd;
	
	mktemp(template);
	fd = open(template, O_CREAT|O_RDWR, 0600);
	if (fd < 0)
		return;
	printf("Created temp file %s, fd = %d\n", template, fd);
	state_array[fd].state = STATE_CREATED;
	strcpy(state_array[fd].name, template);
}

void unlink_file(int fd)
{
	char *filename = state_array[fd].name;
	
	printf("Unlinking %s, fd = %d\n", filename, fd);
	
	unlink(filename);
	state_array[fd].state = STATE_DELETED;
}

void close_file(int fd)
{
	char *filename = state_array[fd].name;
	
	printf("Closing %s, fd = %d\n", filename, fd);
	
	close(fd);
	state_array[fd].state = STATE_CLEAR;
}


main(int argc, char **argv)
{
	int	i, fd;
	
	
	for (i=0; i < 100000; i++) {
		fd = get_random_fd();
		switch (state_array[fd].state) {
		case STATE_CLEAR:
			create_random_file();
			break;
		case STATE_CREATED:
			unlink_file(fd);
			break;
		case STATE_DELETED:
			close_file(fd);
			break;
		}
	}
}


