/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *   Module: fsimext2.c
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <plugin.h>
#include "fsimext2.h"

int fsim_rw_diskblocks( int, int64_t, int32_t, void *, int );
void set_mkfs_options( option_array_t *, char **, logical_volume_t *, char * );
void set_fsck_options( option_array_t *, char **, logical_volume_t * );

/* Vector of plugin record ptrs that we export for the EVMS Engine.  */
plugin_record_t *evms_plugin_records[] = {
	&ext2_plugrec,
	NULL
};

static plugin_record_t  * pMyPluginRecord = &ext2_plugrec;

/*-------------------------------------------------------------------------------------+
+                                                                                      +
+                                   Common Routines                                    +
+                                                                                      +
+--------------------------------------------------------------------------------------*/


/*
 * Get the size limits for this volume.
 */
int fsim_get_volume_limits( struct ext2_super_block * sb,
                         sector_count_t   * fs_min_size,
                         sector_count_t   * fs_max_size,
                         sector_count_t   * vol_max_size)
{
	int rc = 0;
	int		blk_to_sect;

	blk_to_sect = (1 + sb->s_log_block_size);
	*fs_min_size = (sb->s_blocks_count - sb->s_free_blocks_count) << blk_to_sect;
	*fs_max_size = (sector_count_t) 1 << (32+blk_to_sect);
	*vol_max_size = 0xffffffffff;

	LOGEXITRC();
	return rc;
}


/*
 * Un-Format the volume.
 */
int fsim_unmkfs( logical_volume_t * volume )
{
    int  fd;
    int  rc = 0;

    LOGENTRY();
 
    fd = open(EVMS_GET_DEVNAME(volume), O_RDWR|O_EXCL, 0);
    if (fd < 0) return -1;

    if ( volume->private_data ) {
        /* clear private data */
        memset( (void *) volume->private_data, 0, SIZE_OF_SUPER );
        /* zero primary superblock */
        rc =  fsim_rw_diskblocks( fd, EXT2_SUPER_LOC, SIZE_OF_SUPER,
				 volume->private_data, PUT );
    } else {
        rc = ERROR;
    }

    fd = close(fd);

    LOGEXITRC();
    return rc;
}


/*
 * Format the volume.
 */
int fsim_mkfs(logical_volume_t * volume, option_array_t * options )
{
	int     rc = FSIM_ERROR;
	char   *argv[MKFS_EXT2_OPTIONS_COUNT + 6];
	char    logsize[sizeof(unsigned int) + 1];
	pid_t	pidm;
	int     status;

	LOGENTRY();

	/* Fork and execute the correct program. */
    switch (pidm = fork()) {
        
        /* error */
        case -1:
	    rc = EIO;

        /* child */
        case 0:  
            set_mkfs_options( options, argv, volume, logsize );

            /* close stderr, stdout to suppress mke2fs output */
            close(1);
            close(2);
            open("/dev/null", O_WRONLY);
            open("/dev/null", O_WRONLY);

            (void) execvp(argv[0], argv);
            /* using exit() can hang GUI, use _exit */
            _exit(errno);

        /* parent */
        default:
            /* wait for child to complete */
            while (1) {
		    rc = waitpid( pidm, &status, 0 );
		    if (rc == -1) {
			    if (errno == EINTR)
				    continue;
			    rc = errno;
			    goto reterr;
		    } else
			    break;
	    } 
            if ( WIFEXITED(status) ) {
                /* get mke2fs exit code */
                rc = WEXITSTATUS(status);
		if (rc)
			LOG("mke2fs exited with status %d", rc);
            } else {
		    if (WIFSIGNALED(status))
			    LOG("mke2fs died with signal %d",
				WTERMSIG(status));
		    rc = EINTR;
	    }
    }

reterr:
    LOGEXITRC();
    return rc;
}


/*
 * NAME: set_mkfs_options
 *
 * FUNCTION: Build options array (argv) for mkfs.ext2
 *
 * PARAMETERS:
 *      options   - options array passed from EVMS engine
 *      argv      - mkfs options array
 *      vol_name  - volume name on which program will be executed
 *
 */                        
void set_mkfs_options( option_array_t * options, 
                       char ** argv, 
                       logical_volume_t * volume, 
                       char * logsize )
{
    int i, bufsize, opt_count = 2;
    char *buf;

    LOGENTRY();

    argv[0] = "mke2fs";

    /* 'quiet' option */
    argv[1] = "-q";

    /* the following is a big hack to make sure that we don't use a block */
    /* size smaller than hardsector size since this does not work. */
    /* would be nice if the ext2/3 utilities (mkfs) handled this themselves */
    /* also, eventually we will implement this as a user option to manually */
    /* set block size */
    if (volume->object->geometry.bytes_per_sector != EVMS_VSECTOR_SIZE) {
	    switch (volume->object->geometry.bytes_per_sector) {
	    case 2048:
		    argv[2] = "-b2048";
		    opt_count++;
		    break;
	    case 4096:
		    argv[2] = "-b4096";
		    opt_count++;
		    break;
	    default:
		    /* not one we expect, just skip it */
		    break;
	    }
    }

    for ( i=0; i<options->count; i++ ) {

        if ( options->option[i].is_number_based ) {

            switch (options->option[i].number) {
                
            case MKFS_CHECKBB_INDEX:
                /* 'check for bad blocks' option */
                if ( options->option[i].value.bool == TRUE ) {
                    argv[opt_count++] = "-c";
                }
                break;

            case MKFS_CHECKRW_INDEX:
                /* 'check for r/w bad blocks' option */
                if ( options->option[i].value.bool == TRUE ) {
                    argv[opt_count++] = "-cc";
                }
                break;

            case MKFS_JOURNAL_INDEX:
                /* 'create ext3 journal' option */
                if ( options->option[i].value.bool == TRUE ) {
                    argv[opt_count++] = "-j";
                }
                break;

            case MKFS_SETVOL_INDEX:
                /* 'set volume name' option */
                if ( options->option[i].value.s ) {
                    argv[opt_count++] = "-L";
                    argv[opt_count++] = options->option[i].value.s;
                }
                break;

            default:
                break;
            }

        } else {

            if ( !strcmp(options->option[i].name, "badblocks") ) {
                /* 'check for bad blocks' option */
                if ( options->option[i].value.bool == TRUE ) {
                    argv[opt_count++] = "-c";
                }
            }

            if ( !strcmp(options->option[i].name, "badblocks_rw") ) {
                /* 'check for r/w bad blocks' option */
                if ( options->option[i].value.bool == TRUE ) {
                    argv[opt_count++] = "-cc";
                }
            }

            if ( !strcmp(options->option[i].name, "journal") ) {
                /* 'create ext3 journal' option */
                if ( options->option[i].value.bool == TRUE ) {
                    argv[opt_count++] = "-j";
                }
            }

            if ( !strcmp(options->option[i].name, "vollabel") ) {
                /* 'check for bad blocks' option */
                if ( options->option[i].value.s ) {
                    argv[opt_count++] = "-L";
                    argv[opt_count++] = options->option[i].value.s;
                }
            }
        }
    }

    argv[opt_count++] = EVMS_GET_DEVNAME(volume);
    argv[opt_count] = NULL;
     
    bufsize = 0;
    for (i=0; argv[i]; i++)
	    bufsize += strlen(argv[i]) + 5;
    buf = malloc(bufsize+1);
    if (!buf)
	    return;
    buf[0] = 0;
    for (i=0; argv[i]; i++) {
	    strcat(buf, argv[i]);
	    strcat(buf, " ");
    }
    EngFncs->write_log_entry(DEBUG, pMyPluginRecord,
			     "mke2fs command: %s\n", buf);
    free(buf);
    
    LOGEXIT();
    return;
}


/*
 * Run fsck on the volume.
 */
int fsim_fsck(logical_volume_t * volume, option_array_t * options,
	      int *ret_status)
{
	int     rc = FSIM_ERROR;
	char   *argv[FSCK_EXT2_OPTIONS_COUNT + 3];
	pid_t	pidf;
	int     status, bytes_read;
	char    *buffer = NULL;
	int     fds2[2];
	int	banner = 0;

	LOGENTRY();

	/* open pipe, alloc buffer for collecting fsck.jfs output */
	rc = pipe(fds2);
	if (rc) {
	    return(errno);
	}
	if (!(buffer = EngFncs->engine_alloc(MAX_USER_MESSAGE_LEN))) {
	    return(ENOMEM);
	}

	/* Fork and execute the correct program. */
	switch (pidf = fork()) {
        
        /* error */
        case -1:
        	rc = EIO;

        /* child */
        case 0:  
		set_fsck_options( options, argv, volume );

            /* pipe stderr, stdout */
		dup2(fds2[1],1);	/* fds2[1] replaces stdout */
		dup2(fds2[1],2);  	/* fds2[1] replaces stderr */
		close(fds2[0]);	/* don't need this here */

		execvp( argv[0], argv );
		/* should never get here */
		_exit(8);	/* FSCK_ERROR -- operational error */
		
        /* parent */
        default:
		close(fds2[1]);

		/* wait for child to complete */
		fcntl(fds2[0], F_SETFL, fcntl(fds2[0], F_GETFL,0) | O_NONBLOCK);
		while (!(waitpid( pidf, &status, WNOHANG ))) {
			/* read e2fsck output */
			bytes_read = read(fds2[0],buffer,MAX_USER_MESSAGE_LEN);
			if (bytes_read > 0) {
				/* display e2fsck output */
				if (!banner)
					MESSAGE("e2fsck output:");
				banner = 1;
				MESSAGE("%s",buffer);
				memset(buffer,0,bytes_read); /* clear out message  */
			}
			usleep(10000); /* don't hog all the cpu */
		}

		/* do final read, just in case we missed some */
		bytes_read = read(fds2[0],buffer,MAX_USER_MESSAGE_LEN);
		if (bytes_read > 0) {
			if (!banner)
				MESSAGE("e2fsck output:");
			MESSAGE("%s",buffer);
		}
		if ( WIFEXITED(status) ) {
			/* get e2fsck exit code */
			*ret_status = WEXITSTATUS(status);
			LOG("e2fsck completed with exit code %d\n",
			    *ret_status);
			rc = 0;
		} else {
			if (WIFSIGNALED(status))
				LOG("e2fsck died with signal %d",
				    WTERMSIG(status));
			rc = EINTR;
		}
	}

	if (buffer) {
		EngFncs->engine_free(buffer);
	}

	close(fds2[0]);

	LOGEXITRC();
	return rc;
}


/*
 * NAME: set_fsck_options
 *
 * FUNCTION: Build options array (argv) for e2fsck
 *
 * PARAMETERS:
 *      options   - options array passed from EVMS engine
 *      argv      - fsck options array
 *      volume    - volume on which program will be executed
 *
 */                        
void set_fsck_options( option_array_t * options, char ** argv, logical_volume_t * volume )
{
    int i, bufsize, num_opts, opt_count = 1;
    int do_preen = 1;
    char *buf;

    LOGENTRY();

    argv[0] = "e2fsck";

    if (options) 
	    num_opts = options->count;
    else {
	    /* No options, assume force (for resizing) */
	    argv[opt_count++] = "-f";
	    num_opts = 0;
    }
    
    for ( i=0; i < num_opts; i++) {

        if ( options->option[i].is_number_based ) {

            /* 'force check' option */
            if ( (options->option[i].number == FSCK_FORCE_INDEX) && 
                 (options->option[i].value.bool == TRUE) ) {
                argv[opt_count++] = "-f";
            }

            /* 'check read only' option or mounted */
            if ((options->option[i].number == FSCK_READONLY_INDEX) &&
		((options->option[i].value.bool == TRUE) ||
		 EVMS_IS_MOUNTED(volume))) {
                argv[opt_count++] = "-n";
		do_preen = 0;
            }

            /* 'bad blocks check' option and NOT mounted */
            if ( (options->option[i].number == FSCK_CHECKBB_INDEX) && 
                 (options->option[i].value.bool == TRUE)         &&
                 !EVMS_IS_MOUNTED(volume) ) {
                argv[opt_count++] = "-c";
		do_preen = 0;
            }

            /* 'bad blocks check' option and NOT mounted */
            if ( (options->option[i].number == FSCK_CHECKRW_INDEX) && 
                 (options->option[i].value.bool == TRUE)         &&
                 !EVMS_IS_MOUNTED(volume) ) {
                argv[opt_count++] = "-cc";
		do_preen = 0;
            }
	    
            /* timing option */
            if ( (options->option[i].number == FSCK_TIMING_INDEX) && 
		 (options->option[i].value.bool == TRUE) ) {
                argv[opt_count++] = "-tt";
            }
	    
    } else {

            /* 'force check' option selected and NOT mounted */
            if ( !strcmp(options->option[i].name, "force") &&
                 (options->option[i].value.bool == TRUE) &&
                 !EVMS_IS_MOUNTED(volume) ) {
                argv[opt_count++] = "-f";
            }

            /* 'check read only' option selected or mounted */
            if ((!strcmp(options->option[i].name, "readonly")) &&
		((options->option[i].value.bool == TRUE) ||
                 EVMS_IS_MOUNTED(volume))) {
                argv[opt_count++] = "-n";
		do_preen = 0;
            }

            /* 'check badblocks' option selected and NOT mounted */
            if (!strcmp(options->option[i].name, "badblocks") &&
		(options->option[i].value.bool == TRUE) &&
		!EVMS_IS_MOUNTED(volume)) {
                argv[opt_count++] = "-c";
		do_preen = 0;
            }

            /* 'check r/w badblocks' option selected and NOT mounted */
            if (!strcmp(options->option[i].name, "badblocks_rw") &&
		(options->option[i].value.bool == TRUE) &&
		!EVMS_IS_MOUNTED(volume)) {
                argv[opt_count++] = "-cc";
		do_preen = 0;
            }

            /* 'timing' option selected */
            if (!strcmp(options->option[i].name, "badblocks") &&
		(options->option[i].value.bool == TRUE)) {
                argv[opt_count++] = "-tt";
            }
        }
    }

    if (do_preen)
	    argv[opt_count++] = "-p";
    argv[opt_count++] = EVMS_GET_DEVNAME(volume);
    argv[opt_count]   = NULL;

    bufsize = 0;
    for (i=0; argv[i]; i++)
	    bufsize += strlen(argv[i]) + 5;
    buf = malloc(bufsize+1);
    if (!buf)
	    return;
    buf[0] = 0;
    for (i=0; argv[i]; i++) {
	    strcat(buf, argv[i]);
	    strcat(buf, " ");
    }
    EngFncs->write_log_entry(DEBUG, pMyPluginRecord,
			     "fsck command: %s\n", buf);
    free(buf);
    
    LOGEXIT();
    return;
}
/*
 * NAME:ext2fs_swap_super
 *
 * FUNCTION: Swap all fields in the super block to CPU format.
 *
 * PARAMETERS:
 *      sb   - pointer to superblock
 *
 * RETURNS:
 *        void
 */                        
static void ext2fs_swap_super(struct ext2_super_block * sb)
{
	LOGENTRY();
	sb->s_inodes_count = DISK_TO_CPU32(sb->s_inodes_count);
	sb->s_blocks_count = DISK_TO_CPU32(sb->s_blocks_count);
	sb->s_r_blocks_count = DISK_TO_CPU32(sb->s_r_blocks_count);
	sb->s_free_blocks_count = DISK_TO_CPU32(sb->s_free_blocks_count);
	sb->s_free_inodes_count = DISK_TO_CPU32(sb->s_free_inodes_count);
	sb->s_first_data_block = DISK_TO_CPU32(sb->s_first_data_block);
	sb->s_log_block_size = DISK_TO_CPU32(sb->s_log_block_size);
	sb->s_log_frag_size = DISK_TO_CPU32(sb->s_log_frag_size);
	sb->s_blocks_per_group = DISK_TO_CPU32(sb->s_blocks_per_group);
	sb->s_frags_per_group = DISK_TO_CPU32(sb->s_frags_per_group);
	sb->s_inodes_per_group = DISK_TO_CPU32(sb->s_inodes_per_group);
	sb->s_mtime = DISK_TO_CPU32(sb->s_mtime);
	sb->s_wtime = DISK_TO_CPU32(sb->s_wtime);
	sb->s_mnt_count = DISK_TO_CPU16(sb->s_mnt_count);
	sb->s_max_mnt_count = DISK_TO_CPU16(sb->s_max_mnt_count);
	sb->s_magic = DISK_TO_CPU16(sb->s_magic);
	sb->s_state = DISK_TO_CPU16(sb->s_state);
	sb->s_errors = DISK_TO_CPU16(sb->s_errors);
	sb->s_minor_rev_level = DISK_TO_CPU16(sb->s_minor_rev_level);
	sb->s_lastcheck = DISK_TO_CPU32(sb->s_lastcheck);
	sb->s_checkinterval = DISK_TO_CPU32(sb->s_checkinterval);
	sb->s_creator_os = DISK_TO_CPU32(sb->s_creator_os);
	sb->s_rev_level = DISK_TO_CPU32(sb->s_rev_level);
	sb->s_def_resuid = DISK_TO_CPU16(sb->s_def_resuid);
	sb->s_def_resgid = DISK_TO_CPU16(sb->s_def_resgid);
	sb->s_first_ino = DISK_TO_CPU32(sb->s_first_ino);
	sb->s_inode_size = DISK_TO_CPU16(sb->s_inode_size);
	sb->s_block_group_nr = DISK_TO_CPU16(sb->s_block_group_nr);
	sb->s_feature_compat = DISK_TO_CPU32(sb->s_feature_compat);
	sb->s_feature_incompat = DISK_TO_CPU32(sb->s_feature_incompat);
	sb->s_feature_ro_compat = DISK_TO_CPU32(sb->s_feature_ro_compat);
	sb->s_algorithm_usage_bitmap = DISK_TO_CPU32(sb->s_algorithm_usage_bitmap);
	sb->s_journal_inum = DISK_TO_CPU32(sb->s_journal_inum);
	sb->s_journal_dev = DISK_TO_CPU32(sb->s_journal_dev);
	sb->s_last_orphan = DISK_TO_CPU32(sb->s_last_orphan);
	LOGEXIT();
}


/*
 * NAME: fsim_get_ext2_superblock
 *
 * FUNCTION: Get and validate a ext2/3 superblock
 *
 * PARAMETERS:
 *      volume   - pointer to volume from which to get the superblock
 *      sb_ptr   - pointer to superblock
 *
 * RETURNS:
 *      (0) for success
 *      != 0 otherwise
 *        
 */                        
int fsim_get_ext2_superblock( logical_volume_t *volume, struct ext2_super_block *sb_ptr )
{
    int  fd;
    int  rc = 0;

    LOGENTRY();

    fd = open(EVMS_GET_DEVNAME(volume), O_RDONLY, 0);
    if (fd < 0) {
	    rc = EIO;
	    LOGEXITRC();
	    return rc;
    }

    /* get primary superblock */
    rc = fsim_rw_diskblocks( fd, EXT2_SUPER_LOC, SIZE_OF_SUPER, sb_ptr, GET );

    if( rc == 0 ) {
	ext2fs_swap_super(sb_ptr);
        /* see if superblock is ext2/3 */
        if (( sb_ptr->s_magic != EXT2_SUPER_MAGIC ) ||
	    ( sb_ptr->s_rev_level > 1 ))
		rc = FSIM_ERROR;
    }

    close(fd);

    LOGEXITRC();
    return rc;
}


/*
 * NAME: fsim_rw_diskblocks
 *
 * FUNCTION: Read or write specific number of bytes for an opened device.
 *
 * PARAMETERS:
 *      dev_ptr         - file handle of an opened device to read/write
 *      disk_offset     - byte offset from beginning of device for start of disk
 *                        block read/write
 *      disk_count      - number of bytes to read/write
 *      data_buffer     - On read this will be filled in with data read from
 *                        disk; on write this contains data to be written
 *      mode            - GET (read) or PUT (write)
 *
 * RETURNS:
 *      FSIM_SUCCESS (0) for success
 *      ERROR       (-1) can't lseek
 *      EINVAL           
 *      EIO
 *        
 */                        
int fsim_rw_diskblocks( int      dev_ptr,
                        int64_t  disk_offset,
                        int32_t  disk_count,
                        void     *data_buffer,
                        int      mode )
{
    off_t   Actual_Location;
    size_t  Bytes_Transferred;
    int	    rc = 0;

    LOGENTRY();
    
    Actual_Location = lseek(dev_ptr,disk_offset, SEEK_SET);
    if ( ( Actual_Location < 0 ) || ( Actual_Location != disk_offset ) )
        return ERROR;

    switch ( mode ) {
        case GET:
            Bytes_Transferred = read(dev_ptr,data_buffer,disk_count);
            break;
        case PUT:
            Bytes_Transferred = write(dev_ptr,data_buffer,disk_count);
            break;
        default:
	    rc = EINVAL;
	    LOGEXITRC();
            return rc;
            break;
    }

    if ( Bytes_Transferred != disk_count ) {
        rc = EIO;
	LOGEXITRC();
        return rc;
    }

    LOGEXIT();
    return FSIM_SUCCESS;
}


/*
 * Test e2fsprogs version.
 *
 * We don't bother since we don't need any special functionality that
 * hasn't been around for *years*
 */	
int fsim_test_version( )
{
	return 0;
}





