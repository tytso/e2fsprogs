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
 *   Module: fs_ext2.c
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <plugin.h>
#include <sys/wait.h>
#include "fsimext2.h"

static plugin_record_t    *pMyPluginRecord = &ext2_plugrec;

/*-------------------------------------------------------------------------------------+
+                                                                                      +
+                            Start Of EVMS Plugin Functions                            +
+                        (exported to engine via function table)                       +
+                                                                                      +
+-------------------------------------------------------------------------------------*/


#if (EVMS_ABI_CODE >= 120)
static int fs_setup( engine_functions_t *engine_function_table)
#else
static int fs_setup( engine_mode_t mode, engine_functions_t *engine_function_table)
#endif
{
	int rc = 0;
	EngFncs = engine_function_table;

	LOGENTRY();

	/*
	 * We don't really care about the e2fsprogs version, but we leave
	 * this here in case we do at a later date....
	 */
	rc = fsim_test_version();
#if 0
	if ( rc ) {
		LOG_WARNING( "e2fsprogs must be version 1.XXX or later to function properly with this FSIM.\n" );
		LOG_WARNING( "Please get the current version of e2fsprogs from http://e2fsprogs.sourceforge.net\n" );
		rc = ENOSYS;
	}
#endif
	LOGEXIT();
	return rc;
}


/*
 * Free all of the private data item we have left on volumes.
 */
static void fs_cleanup()
{
	int rc = 0;
	dlist_t global_volumes;
	union{logical_volume_t *lvt; void *vp;}volume;
	LOGENTRY();

	rc = EngFncs->get_volume_list(pMyPluginRecord, &global_volumes);
	if (!rc) {
		while (ExtractObject(global_volumes, sizeof(logical_volume_t), VOLUME_TAG, NULL, &volume.vp)==0) {
			if (volume.lvt->private_data) {
				EngFncs->engine_free(volume.lvt->private_data);
			}
		}
	}
	LOGEXIT();
}


/*
 * Does this FSIM manage the file system on this volume?
 * Return 0 for "yes", else a reason code.
 */
static int fs_probe(logical_volume_t * volume)
{
	int  rc = 0;
	struct ext2_super_block *sb_ptr;

	LOGENTRY();

    /* allocate space for copy of superblock in private data */
    sb_ptr = EngFncs->engine_alloc( SIZE_OF_SUPER );

    if ( sb_ptr ) {
        memset( (void *) sb_ptr, 0, SIZE_OF_SUPER );

        /* get and validate ext2 superblock */
        rc = fsim_get_ext2_superblock( volume, sb_ptr );

        if ( !rc ) {
            /* store copy of valid EXT2/3 superblock in private data */
            volume->private_data = (void*)sb_ptr;
        } else {
            /* could not get valid EXT2/3 superblock */
            volume->private_data = NULL;
        }
    } else {
        rc = ENOMEM;
    }

	LOGEXITRC();
	return rc;
}


/*
 * Can mkfs this volume?
 */
static int fs_can_mkfs(logical_volume_t * volume)
{
	int  rc=0;

	LOGENTRY();

    /*****************************************************
     *  FUTURE - ensure mke2fs exists                    *
     *****************************************************/

	if (EVMS_IS_MOUNTED(volume)) {
		/* If mounted, can't format. */
		rc = EBUSY;
	} else if ((volume->vol_size * PBSIZE) < MINEXT2) {
		/* voluem size must be >= MINEXT2 */
		rc = EPERM;
    }

	LOGEXITRC();
	return rc;
}


/*
 * Can unmkfs this volume?
 */
static int fs_can_unmkfs(logical_volume_t * volume)
{
	int  rc=0;

	LOGENTRY();

	if (EVMS_IS_MOUNTED(volume)) {
		/* If mounted, can't unmkfs. */
		rc = EBUSY;
	}

	LOGEXITRC();
	return rc;

}


/*
 * Can fsck this volume?
 */
static int fs_can_fsck(logical_volume_t * volume)
{
	int  rc=0;

	LOGENTRY();

    /*****************************************************
     *  FUTURE - ensure e2fsck exists                    *
     *           match version with available functions  *
     *****************************************************/

	LOGEXITRC();
	return rc;
}


/*
 * Get the current size of this volume
 */
static int fs_get_fs_size( logical_volume_t * volume,
            			   sector_count_t   * size    )
{
	int  rc = EINVAL;
	struct ext2_super_block *sb = (struct ext2_super_block *)volume->private_data;

	LOGENTRY();

	if (!sb) {
		LOGEXITRC();
		return rc;
	}

    /* get and validate current ext2/3 superblock */
    rc = fsim_get_ext2_superblock( volume, sb );

    if (!rc && sb) {
		*size = sb->s_blocks_count << (1 + sb->s_log_block_size);
		rc = 0;
	}

	LOGEXITRC();
	return rc;
}


/*
 * Get the size limits for this volume.
 */
static int fs_get_fs_limits( logical_volume_t * volume,
               				 sector_count_t   * fs_min_size,
               				 sector_count_t   * fs_max_size,
               				 sector_count_t   * vol_max_size)
{
	int rc = EINVAL;
	struct ext2_super_block *sb_ptr = (struct ext2_super_block *) volume->private_data;

	LOGENTRY();

	if (!sb_ptr) {
		LOGEXITRC();
		return rc;
	}

    /* get and validate current ext2 superblock */
    rc = fsim_get_ext2_superblock( volume, sb_ptr );
    
    if ( !rc ) {
	    rc = fsim_get_volume_limits( sb_ptr, fs_min_size, fs_max_size, vol_max_size);
	    LOG_EXTRA("volume:%s, min:%lld, max:%lld\n",EVMS_GET_DEVNAME(volume), *fs_min_size, *fs_max_size);
	    LOG_EXTRA("fssize:%lld, vol_size:%lld\n",volume->fs_size,volume->vol_size );

	    if (*fs_min_size > volume->vol_size) {
		    LOG_ERROR("EXT2 FSIM returned min size > volume size, setting min size to volume size\n");
		    *fs_min_size = volume->vol_size;
	    }
    }

	LOGEXITRC();
	return rc;
}


/*
 * Expand the volume to new_size.  If the volume is not expanded exactly to
 * new_size, set new_sie to the new_size of the volume.
 */
static int fs_expand( logical_volume_t * volume,
		              sector_count_t   * new_size )
{
	struct ext2_super_block *sb;
	int     rc = 0;
	char   *argv[7];
	pid_t   pidf;
	int     status;
	int     fds1[2];  /* pipe for stdin 0=read 1=write */
	int     fds2[2];  /* pipe for stderr and stdout 0=-read,1=write */
	int     bytes_read;
	char    *buffer = NULL;
	int	banner = 0;

	LOGENTRY();

	/* get and validate current ext2/3 superblock */
	sb = (struct ext2_super_block *) volume->private_data;	
	rc = fsim_get_ext2_superblock( volume, sb );
	if (rc) {
		goto errout;
	}
	if ((sb->s_lastcheck < sb->s_mtime) ||
	    (sb->s_state & EXT2_ERROR_FS) ||
	    ((sb->s_state & EXT2_VALID_FS) == 0)) {
		MESSAGE("Running fsck before expanding volume");
		rc = fsim_fsck(volume, NULL, &status );
		if (rc) {
			MESSAGE("Attempt to execute fsck failed (%d)", rc);
			MESSAGE("Aborting volume expand");
			goto errout;
		}
		if (status >= 4) {
			MESSAGE("Aborting volume expand");
			rc = status;
			goto errout;
		}
	}
	
	/* don't expand if mounted */
	if (EVMS_IS_MOUNTED(volume)) {
		rc = EBUSY;
		goto errout;
	}

	if (pipe(fds1)) {
		rc = errno;
		goto errout;
	}
	if (pipe(fds2)) {
		rc = errno;
		goto errout;
	}
	if (!(buffer = EngFncs->engine_alloc(MAX_USER_MESSAGE_LEN))) {
		rc = ENOMEM;
		goto errout;
	}

	/* Fork and execute the correct program. */
	switch (pidf = fork()) {

	/* error */
	case -1:
		return EIO;

		/* child */
	case 0:  
		argv[0] = "resize2fs";
		SET_STRING_FIELD(argv[1], EVMS_GET_DEVNAME(volume));
		argv[2] = NULL;

		dup2(fds1[0],0);	/* fds1[0] replaces stdin */
		dup2(fds2[1],1);	/* fds2[1] replaces stdout */
		dup2(fds2[1],2);	/* fds2[1] replaces stderr */
		close(fds2[0]);		/* don't need this here */
		close(fds1[1]);		/* don't need this here */

		rc = execvp( argv[0], argv );

		/* using exit() can hang GUI, use _exit */
		_exit(errno);

		/* parent */
	default:
		/*
		 * WARNING: Do Not close read handle of stdin or
		 *  you will cause a SIGPIPE if you write after the 
		 * child process has gone away.
		 */
/*		close(fds1[0]);  */
		close(fds2[1]);

		/* wait for child to complete */
		fcntl(fds2[0], F_SETFL, fcntl(fds2[0], F_GETFL,0) | O_NONBLOCK);
		while (!(waitpid( pidf, &status, WNOHANG ))) {
			bytes_read = read(fds2[0],buffer,MAX_USER_MESSAGE_LEN);
			if (bytes_read > 0) {
				if (!banner)
					MESSAGE("expand output:");
				banner = 1;
				MESSAGE("%s", buffer);
				memset(buffer,0,bytes_read); /* clear out message  */
			}
			usleep(10000); /* don't hog all the cpu */
		}
		/* do final read, just in case we missed some */
		bytes_read = read(fds2[0],buffer,MAX_USER_MESSAGE_LEN);
		if (bytes_read > 0) {
			if (!banner)
				MESSAGE("expand output:");
			MESSAGE("%s",buffer);
		}
		if ( WIFEXITED(status) ) {
			/* get expand exit code */
			rc = WEXITSTATUS(status);
			if (rc)
				LOG("Expand completed successfully\n");
			else
				LOG("Expand completed with rc = %d\n", status);
		} else {
			if (WIFSIGNALED(status))
				LOG("Expand died with signal %d",
				    WTERMSIG(status));
			rc = EINTR;
		}
	}
	if (buffer) {
		EngFncs->engine_free(buffer);
	}
	fs_get_fs_size(volume, new_size);
errout:
	LOGEXITRC();
	return rc;
}


/*
 * "unmkfs" the volume
 */
static int fs_unmkfs(logical_volume_t * volume)
{
	int rc = EINVAL;
	LOGENTRY();

	if (EVMS_IS_MOUNTED(volume)) {
		/* If mounted, can't unmkfs. */
		rc = EBUSY;
	} else if ( (rc = fsim_unmkfs(volume)) == FSIM_SUCCESS ){
	    volume->private_data = NULL;
	}
    
	LOGEXITRC();
	return rc;
}


/*
 * Shrink the volume to new_size.  If the volume is not expanded exactly to
 * new_size, set new_size to the new_size of the volume.
 */
static int fs_shrink( logical_volume_t * volume,
        		      sector_count_t     requested_size,
		              sector_count_t   * new_size )
{
	int     rc = 0;
	char   *argv[7];
	pid_t   pidf;
	int     status;
	int     fds1[2];  /* pipe for stdin 0=read 1=write */
	int     fds2[2];  /* pipe for stderr and stdout 0=-read,1=write */
	int     bytes_read;
	char    *buffer = NULL;
	char    size_buf[128];
	struct ext2_super_block *sb;
	int	banner = 0;

	LOGENTRY();

	/* don't shrink if mounted */
	if (EVMS_IS_MOUNTED(volume)) {
		LOGEXITRC();
		return EBUSY;
	}

	/* get and validate current ext2/3 superblock */
	sb = (struct ext2_super_block *) volume->private_data;	
	rc = fsim_get_ext2_superblock( volume, sb );
	if (rc) {
		goto errout;
	}

	requested_size = requested_size >> (1 + sb->s_log_block_size);
	if ((sb->s_lastcheck < sb->s_mtime) ||
	    (sb->s_state & EXT2_ERROR_FS) ||
	    ((sb->s_state & EXT2_VALID_FS) == 0)) {
		MESSAGE("Running fsck before shrinking volume");
		rc = fsim_fsck(volume, NULL, &status );
		if (rc) {
			MESSAGE("Attempt to execute fsck failed (%d)", rc);
			MESSAGE("Aborting volume shrink");
			goto errout;
		}
		if (status >= 4) {
			MESSAGE("Aborting volume shrink");
			rc = status;
			goto errout;
		}
	}
	    
	if (pipe(fds1)) {
		rc = errno;
		goto errout;
	}
	if (pipe(fds2)) {
		rc = errno;
		goto errout;
	}
	if (!(buffer = EngFncs->engine_alloc(MAX_USER_MESSAGE_LEN))) {
		rc = ENOMEM;
		goto errout;
	}

	/* Fork and execute the correct program. */
	switch (pidf = fork()) {

	/* error */
	case -1:
		return EIO;

		/* child */
	case 0:  
		argv[0] = "resize2fs";
		SET_STRING_FIELD(argv[1], EVMS_GET_DEVNAME(volume));
		sprintf(size_buf,"%lld", (sector_count_t)requested_size);
		argv[2] = size_buf;
		argv[3] = NULL;

		dup2(fds1[0],0);	/* fds1[0] replaces stdin */
		dup2(fds2[1],1);	/* fds2[1] replaces stdout */
		dup2(fds2[1],2);	/* fds2[1] replaces stderr */
		close(fds2[0]);		/*  don't need this here */
		close(fds1[1]);		/*  don't need this here */

		rc = execvp( argv[0], argv );

		/* using exit() can hang GUI, use _exit */
		_exit(errno);

		/* parent */
	default:
		/*
		 * WARNING: Do Not close read handle of stdin or you
		 * will cause a SIGPIPE if you write after the child
		 * process has gone away.
		 */
  /*		close(fds1[0]);  */
		close(fds2[1]);
		write(fds1[1], "Yes\n",4);  

		fcntl(fds2[0], F_SETFL, fcntl(fds2[0], F_GETFL,0) | O_NONBLOCK);
		/* wait for child to complete */
		while (!(waitpid( pidf, &status, WNOHANG ))) {
			bytes_read = read(fds2[0],buffer,MAX_USER_MESSAGE_LEN);
			if (bytes_read > 0) {
				if (!banner)
					MESSAGE("Shrink output:");
				banner = 1;
				MESSAGE("%s", buffer);
				memset(buffer,0,bytes_read); /* clear out message  */
			}
			usleep(10000); /* don't hog all the cpu */
		}
		/* do final read, just in case we missed some */
		bytes_read = read(fds2[0],buffer,MAX_USER_MESSAGE_LEN);
		if (bytes_read > 0) {
			if (!banner)
				MESSAGE("Shrink output:");
			MESSAGE("%s",buffer);
		}
		if ( WIFEXITED(status) ) {
			/* get shrink exit code */
			rc = WEXITSTATUS(status);
			if (rc) 
				LOG("Shrink completed successfully\n");
			else
				LOG("Shrink completed with rc = %d\n",status);
		} else {
			if (WIFSIGNALED(status))
				LOG("Shrink died with signal %d",
				    WTERMSIG(status));
			rc = EINTR;
		}
	}
	if (buffer) {
		EngFncs->engine_free(buffer);
	}
	fs_get_fs_size(volume, new_size);
errout:
	LOGEXITRC();
	return rc;
}



/*
 * Format the volume.
 */
static int fs_mkfs(logical_volume_t * volume, option_array_t * options )
{
	int  rc = 0;

	LOGENTRY();

	/* don't format if mounted */
	if (EVMS_IS_MOUNTED(volume)) {
		rc = EBUSY;
		goto errout;
	}

	rc = fsim_mkfs(volume, options);

	/* probe to set up private data */
	if (!rc) {
		rc = fs_probe(volume);
	}

errout:
	LOGEXITRC();
	return rc;
}


/*
 * Run fsck on the volume.
 */
static int fs_fsck(logical_volume_t * volume, option_array_t * options )
{
	int rc = EINVAL;
	int status;

	LOGENTRY();

	rc = fsim_fsck( volume, options, &status );
	if (rc)
		goto errout;
		
	/*
	 * If the volume is mounted, e2fsck checked READ ONLY
	 * regardless of options specified.  If the check was READ
	 * ONLY and errors were found, let the user know how to fix
	 * them.
	 */
	if (EVMS_IS_MOUNTED(volume) && (status & FSCK_ERRORS_UNCORRECTED)) {
		MESSAGE( "%s is mounted.", EVMS_GET_DEVNAME(volume) );
		MESSAGE( "e2fsck checked the volume READ ONLY and found, but did not fix, errors." );
		MESSAGE( "Unmount %s and run e2fsck again to repair the file system.", EVMS_GET_DEVNAME(volume) );
	}
	if (status > 4) {
		MESSAGE( "e2fsck exited with status code %d.", status);
	}

errout:
	LOGEXITRC();
	return rc;
}


/*
 * Return the total number of supported options for the specified task.
 */
static int fs_get_option_count(task_context_t * context)
{
    int count = 0;

	LOGENTRY();

	switch(context->action) {
	    case EVMS_Task_mkfs:
		    count = MKFS_EXT2_OPTIONS_COUNT;
		    break;
	    case EVMS_Task_fsck:
		    count = FSCK_EXT2_OPTIONS_COUNT;
		    break;
	    default:
		    count = -1;
		    break;
	}

	LOGEXIT();
	return count;
}


/*
 * Fill in the initial list of acceptable objects.  Fill in the minimum and
 * maximum nuber of objects that must/can be selected.  Set up all initial
 * values in the option_descriptors in the context record for the given
 * task.  Some fields in the option_descriptor may be dependent on a
 * selected object.  Leave such fields blank for now, and fill in during the
 * set_objects call.
 */
static int fs_init_task( task_context_t * context )
{
	dlist_t global_volumes;
	union{logical_volume_t *lvt; void *vp;}volume;
	void* waste;
	int size;
	union{unsigned long u; TAG t;}tag;
	int  rc = 0;
	option_descriptor_t	*opt;
	
	LOGENTRY();

	context->min_selected_objects = 1;
	context->max_selected_objects = 1;
	context->option_descriptors->count = 0;

	/* Parameter check */
	if (!context) {
		rc = EFAULT;
		goto errout;
	}

	rc = EngFncs->get_volume_list(NULL, &global_volumes);

	while (!(rc = BlindExtractObject(global_volumes, &size, &tag.t, NULL, &volume.vp))) {

		switch (context->action) {
		case EVMS_Task_mkfs:
			/* only mkfs unformatted volumes */
			if ((volume.lvt->file_system_manager == NULL) &&
			    !EVMS_IS_MOUNTED(volume.lvt) &&
			    ((volume.lvt->vol_size * PBSIZE) > MINEXT2)) {
				rc = InsertObject(context->acceptable_objects, sizeof(logical_volume_t), volume.lvt, VOLUME_TAG, NULL, InsertAtStart, TRUE, (void **)&waste);
			}
			break;

		case EVMS_Task_fsck:					 
			/* only fsck our stuff */
			if (volume.lvt->file_system_manager == &ext2_plugrec) {
				rc = InsertObject(context->acceptable_objects, sizeof(logical_volume_t), volume.lvt, VOLUME_TAG, NULL, InsertAtStart, TRUE, (void **)&waste);
			}
			break;

		default:
			rc = EINVAL;
			break;
		}
	}

	if (rc == DLIST_EMPTY || rc == DLIST_END_OF_LIST) {
		rc = 0;
	}

	switch (context->action) {
	
	case EVMS_Task_mkfs:

		context->option_descriptors->count = MKFS_EXT2_OPTIONS_COUNT;

		/* check for bad blocks option */
		opt = &context->option_descriptors->option[MKFS_CHECKBB_INDEX];
		SET_STRING(opt->name, "badblocks" );
		SET_STRING(opt->title, "Check For Bad Blocks" );
		SET_STRING(opt->tip, "Check the volume for bad blocks before building the file system." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = FALSE;

		/* check for r/w bad blocks option */
		opt = &context->option_descriptors->option[MKFS_CHECKRW_INDEX];
		SET_STRING(opt->name, "badblocks_rw" );
		SET_STRING(opt->title, "RW Check For Bad Blocks" );
		SET_STRING(opt->tip, "Do a read/write check for bad blocks before building the file system." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = FALSE;

		/* Set Volume Label option */
		opt = &context->option_descriptors->option[MKFS_SETVOL_INDEX];
		SET_STRING(opt->name, "vollabel" );
		SET_STRING(opt->title, "Volume Label" );
		SET_STRING(opt->tip, "Set the volume label for the file system." );
		opt->help = NULL;
		opt->type = EVMS_Type_String;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 16;
#else
		opt->min_len = 0;
		opt->max_len = 16;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED | EVMS_OPTION_FLAGS_NO_INITIAL_VALUE;
		opt->constraint_type = EVMS_Collection_None;
		opt->value.s = EngFncs->engine_alloc(17);
		if (opt->value.s == NULL) {
			LOGEXIT();
			return ENOMEM;
		}

		/* create ext3 journal option */
		opt = &context->option_descriptors->option[MKFS_JOURNAL_INDEX];
		SET_STRING(opt->name, "journal" );
		SET_STRING(opt->title, "Create Ext3 Journal" );
		SET_STRING(opt->tip, "Create a journal for use with the ext3 file system." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = TRUE;

		break;

	case EVMS_Task_fsck:

		context->option_descriptors->count = FSCK_EXT2_OPTIONS_COUNT;

		/* force check option */
		opt = &context->option_descriptors->option[FSCK_FORCE_INDEX];
		SET_STRING(opt->name, "force" );
		SET_STRING(opt->title, "Force Check" );
		SET_STRING(opt->tip, "Force complete file system check." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = FALSE;

		/* read-only check option */
		opt = &context->option_descriptors->option[FSCK_READONLY_INDEX];
		SET_STRING(opt->name, "readonly" );
		SET_STRING(opt->title, "Check Read-Only" );
		SET_STRING(opt->tip, "Check the file system READ ONLY.  Report but do not correct errors." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		opt->constraint_type = EVMS_Collection_None;
		/* if volume is mounted, only possible fsck.ext2 options is READONLY */
		if (EVMS_IS_MOUNTED(context->volume)) {
			opt->value.bool = TRUE;
		} else {
			opt->value.bool = FALSE;
		}

		/* check for bad blocks option */
		opt = &context->option_descriptors->option[FSCK_CHECKBB_INDEX];
		SET_STRING(opt->name, "badblocks" );
		SET_STRING(opt->title, "Check For Bad Blocks" );
		SET_STRING(opt->tip, "Check for bad blocks and mark them as busy." );
		opt->help = NULL;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
		if (EVMS_IS_MOUNTED(context->volume)) {
			opt->flags = EVMS_OPTION_FLAGS_INACTIVE;
		} else {
			opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		}
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = FALSE;

		/* check for r/w bad blocks option */
		opt = &context->option_descriptors->option[FSCK_CHECKRW_INDEX];
		SET_STRING(opt->name, "badblocks_rw" );
		SET_STRING(opt->title, "RW Check For Bad Blocks" );
		SET_STRING(opt->tip, "Do a read/write check for bad blocks and mark them as busy." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		if (EVMS_IS_MOUNTED(context->volume)) {
			opt->flags = EVMS_OPTION_FLAGS_INACTIVE;
		} else {
			opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED;
		}
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = FALSE;

		/* timing option */
		opt = &context->option_descriptors->option[FSCK_TIMING_INDEX];
		SET_STRING(opt->name, "timing" );
		SET_STRING(opt->title, "Timing Statistics" );
		SET_STRING(opt->tip, "Print timing statistics." );
		opt->help = NULL;
		opt->type = EVMS_Type_Boolean;
		opt->unit = EVMS_Unit_None;
#if (EVMS_ABI_CODE == 100)
		opt->size = 0;
#endif
		opt->flags = EVMS_OPTION_FLAGS_NOT_REQUIRED | EVMS_OPTION_FLAGS_INACTIVE;
		opt->constraint_type = EVMS_Collection_None;
		opt->value.bool = FALSE;
		break;

	default:
		rc = EINVAL;
		break;
	}

errout:
	LOGEXITRC();
	return rc;
}


/*
 * Examine the specified value, and determine if it is valid for the task
 * and option_descriptor index. If it is acceptable, set that value in the
 * appropriate entry in the option_descriptor. The value may be adjusted
 * if necessary/allowed. If so, set the effect return value accordingly.
 */
static int fs_set_option( task_context_t * context,
			              u_int32_t        index,
			              value_t        * value,
			              task_effect_t  * effect )
{
	int  rc= 0, other;

	LOGENTRY();

	/* Parameter check */
	if (!context || !value || !effect) {
		rc = EFAULT;
		goto errout;
	}

	*effect = 0;

	switch (context->action) {
	
	case EVMS_Task_mkfs:
		switch (index) {
		
		case MKFS_CHECKBB_INDEX:
		case MKFS_CHECKRW_INDEX:
			/* Conflicts with each other */
			if (index == MKFS_CHECKBB_INDEX)
				other = MKFS_CHECKRW_INDEX;
			else
				other = MKFS_CHECKBB_INDEX;
			if (context->option_descriptors->option[other].value.bool) {
				context->option_descriptors->option[other].value.bool = FALSE;
				*effect = EVMS_Effect_Reload_Options;
			}
			/* Fall through */
			
		case MKFS_JOURNAL_INDEX:
			context->option_descriptors->option[index].value.bool = value->bool;
			break;

		case MKFS_SETVOL_INDEX:
			/* 'set volume label' option set? */
			strncpy(context->option_descriptors->option[index].value.s, value->s, 16);
			break;

		default:
			break;
        }
        break;

    case EVMS_Task_fsck:
        switch (index) {

        case FSCK_READONLY_INDEX:
            /* 'check read only' option set? */
            context->option_descriptors->option[index].value.bool = value->bool;

            /* If mounted, only allow 'yes' for check read only */
	    if (EVMS_IS_MOUNTED(context->volume) && !value->bool) {
                context->option_descriptors->option[index].value.bool = TRUE;
                *effect = EVMS_Effect_Reload_Options;
            }

	    /* If read-only, we can't check for bad blocks */
	    if (context->option_descriptors->option[FSCK_CHECKBB_INDEX].value.bool ||
		context->option_descriptors->option[FSCK_CHECKRW_INDEX].value.bool) {
 		    context->option_descriptors->option[FSCK_CHECKBB_INDEX].value.bool = FALSE;
 		    context->option_descriptors->option[FSCK_CHECKRW_INDEX].value.bool = FALSE;
		    *effect = EVMS_Effect_Reload_Options;
		    break;
	    }
	    
	    break;

	case FSCK_CHECKBB_INDEX:
        case FSCK_CHECKRW_INDEX:
	    if (EVMS_IS_MOUNTED(context->volume) && value->bool) {
                MESSAGE("Can't check for bad blocks when the volume is mounted.");
                context->option_descriptors->option[index].value.bool = FALSE;
                *effect = EVMS_Effect_Reload_Options;
                break;
            }

	    /* Conflicts with each other */
	    if (index == FSCK_CHECKBB_INDEX)
		    other = FSCK_CHECKRW_INDEX;
	    else
		    other = FSCK_CHECKBB_INDEX;
	    if (context->option_descriptors->option[other].value.bool) {
 		    context->option_descriptors->option[other].value.bool = FALSE;
		    *effect = EVMS_Effect_Reload_Options;
	    }
	    
	    /* Conflicts with read-only option */
	    if (context->option_descriptors->option[FSCK_READONLY_INDEX].value.bool) {
 		    context->option_descriptors->option[FSCK_READONLY_INDEX].value.bool = FALSE;
		    *effect = EVMS_Effect_Reload_Options;
	    }
	    
	    /* Fall Through */

        case FSCK_FORCE_INDEX:
        case FSCK_TIMING_INDEX:
            context->option_descriptors->option[index].value.bool = value->bool;

	    break;

        default:
            break;
        }
        break;

    default:
        break;
    }

errout:
	LOGEXITRC();
	return rc;
}


/*
 * Validate the volumes in the selected_objects dlist in the task context.
 * Remove from the selected objects lists any volumes which are not
 * acceptable.  For unacceptable volumes, create a declined_handle_t
 * structure with the reason why it is not acceptable, and add it to the
 * declined_volumes dlist.  Modify the accepatble_objects dlist in the task
 * context as necessary based on the selected objects and the current
 * settings of the options.  Modify any option settings as necessary based
 * on the selected objects.  Return the appropriate task_effect_t settings
 * if the object list(s), minimum or maximum objects selected, or option
 * settings have changed.
 */
static int fs_set_volumes( task_context_t * context,
            			   dlist_t          declined_volumes,	 /* of type declined_handle_t */
			               task_effect_t  * effect )
{
	int  rc = 0;
	union{logical_volume_t *lvt; ADDRESS addr;}vol;

	LOGENTRY();

	if (effect)
		*effect = 0;

	if (context->action == EVMS_Task_mkfs) {
	
        /* get the selected volume */
        rc = GetObject(context->selected_objects,sizeof(logical_volume_t),VOLUME_TAG,NULL,FALSE, &vol.addr);

        if (!rc) {
	    if (EVMS_IS_MOUNTED(vol.lvt)) {
                /* If mounted, can't mkfs.ext2. */
                rc = EBUSY;
            } else {
                if ( (vol.lvt->vol_size * PBSIZE) < MINEXT2) {
                    
                    /*****************************************************
                     *  FUTURE - move this volume to unacceptable list   *
                     *****************************************************/

                    MESSAGE( "The size of volume %s is %d bytes.", EVMS_GET_DEVNAME(vol.lvt), vol.lvt->vol_size * PBSIZE );
                    MESSAGE( "mke2fs requires a minimum of %u bytes to build the ext2/3 file system.", MINEXT2 );
                    rc = EPERM;
                }
            }
        }
    }

	LOGEXITRC();
	return rc;
}


/*
 * Return any additional information that you wish to provide about the
 * volume.  The Engine privides an external API to get the information
 * stored in the logical_volume_t.  This call is to get any other
 * information about the volume that is not specified in the
 * logical_volume_t.  Any piece of information you wish to provide must be
 * in an extended_info_t structure.  Use the Engine's engine_alloc() to
 * allocate the memory for the extended_info_t.  Also use engine_alloc() to
 * allocate any strings that may go into the extended_info_t.  Then use
 * engine_alloc() to allocate an extended_info_array_t with enough entries
 * for the number of exteneded_info_t structures you are returning.  Fill
 * in the array and return it in *info.
 * If you have extended_info_t descriptors that themselves may have more
 * extended information, set the EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag
 * in the extended_info_t flags field.  If the caller wants more information
 * about a particular extended_info_t item, this API will be called with a
 * pointer to the sotrage_object_t and with a pointer to the name of the
 * extended_info_t item.  In that case, return an extended_info_array_t with
 * further information about the item.  Each of those items may have the
 * EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag set if you desire.  It is your
 * resposibility to give the items unique names so that you know which item
 * the caller is asking additional information for.  If info_name is NULL,
 * the caller just wants top level information about the object.
 */
static int fs_get_volume_info( logical_volume_t        * volume,
            			       char                    * info_name,
            			       extended_info_array_t * * info )
{
	int rc = EINVAL;
	extended_info_array_t  *Info;
	struct ext2_super_block      *sb_ptr = (struct ext2_super_block *)volume->private_data;


	LOGENTRY();

	if (!sb_ptr) {
		LOGEXITRC();
		return rc;
	}

    /* get and validate current ext2 superblock */
	rc = fsim_get_ext2_superblock( volume, sb_ptr );

	if (info_name || rc) {
		rc = EINVAL;
		goto errout;
	}

	/* reset limits. */
	fs_get_fs_limits( volume, &volume->min_fs_size,
			  &volume->max_fs_size, &volume->max_vol_size);

	Info = EngFncs->engine_alloc( sizeof(extended_info_array_t) + ( 5 * sizeof(extended_info_t) ) );

	if (!Info) {
		rc = ENOMEM;
		goto errout;
	}

	Info->count = 5;

	SET_STRING_FIELD( Info->info[0].name, "Version" );
	SET_STRING_FIELD( Info->info[0].title, "Ext2 Revision Number" );
	SET_STRING_FIELD( Info->info[0].desc, "Ext2 Revision Number.");
	Info->info[0].type               = EVMS_Type_Unsigned_Int32;
	Info->info[0].unit               = EVMS_Unit_None;
	Info->info[0].value.ui32         = sb_ptr->s_rev_level;
	Info->info[0].collection_type    = EVMS_Collection_None;
	memset( &Info->info[0].group, 0, sizeof(group_info_t));

	SET_STRING_FIELD( Info->info[1].name, "State" );
	SET_STRING_FIELD( Info->info[1].title, "Ext2 State" );
	SET_STRING_FIELD( Info->info[1].desc, "The state of Ext2.");
	Info->info[1].type               = EVMS_Type_String;
	Info->info[1].unit               = EVMS_Unit_None;
	if (sb_ptr->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER) {
                SET_STRING_FIELD(Info->info[1].value.s, "Needs journal replay");
	} else if (sb_ptr->s_state & EXT2_ERROR_FS) {
                SET_STRING_FIELD(Info->info[1].value.s, "Had errors");
	} else if (sb_ptr->s_state & EXT2_VALID_FS) {
                SET_STRING_FIELD(Info->info[1].value.s, "Clean");
	} else {
                SET_STRING_FIELD(Info->info[1].value.s, "Dirty");
	}
	Info->info[1].collection_type    = EVMS_Collection_None;
	memset( &Info->info[1].group, 0, sizeof(group_info_t));

	SET_STRING_FIELD( Info->info[2].name, "VolLabel" );
	SET_STRING_FIELD( Info->info[2].title, "Volume Label" );
	SET_STRING_FIELD( Info->info[2].desc, "File system volume label.");
	Info->info[2].type               = EVMS_Type_String;
	Info->info[2].unit               = EVMS_Unit_None;

	Info->info[2].value.s = EngFncs->engine_alloc(17);
	if (!Info->info[2].value.s)
		return -ENOMEM;
	Info->info[2].value.s[16] = 0;
	memcpy(Info->info[2].value.s, sb_ptr->s_volume_name, 16);
	Info->info[2].collection_type    = EVMS_Collection_None;
	memset( &Info->info[2].group, 0, sizeof(group_info_t));

	SET_STRING_FIELD( Info->info[3].name, "Size" );
	SET_STRING_FIELD( Info->info[3].title, "File System Size" );
	SET_STRING_FIELD( Info->info[3].desc, "Size of the file system.");
	Info->info[3].type               = EVMS_Type_Unsigned_Int64;
	Info->info[3].unit               = EVMS_Unit_Sectors;
	Info->info[3].value.ui64         = sb_ptr->s_blocks_count <<
		(1 + sb_ptr->s_log_block_size);
	Info->info[3].collection_type    = EVMS_Collection_None;
	memset( &Info->info[3].group, 0, sizeof(group_info_t));

	SET_STRING_FIELD( Info->info[4].name, "FreeSpace" );
	SET_STRING_FIELD( Info->info[4].title, "Free File System Space" );
	SET_STRING_FIELD( Info->info[4].desc, "Amount of unused space in the file system.");
	Info->info[4].type               = EVMS_Type_Unsigned_Int64;
	Info->info[4].unit               = EVMS_Unit_Sectors;
	Info->info[4].value.ui64         = sb_ptr->s_free_blocks_count <<
		(1 + sb_ptr->s_log_block_size);
	Info->info[3].collection_type    = EVMS_Collection_None;
	memset( &Info->info[3].group, 0, sizeof(group_info_t));

	*info = Info;
	
	rc = 0;

errout:
	LOGEXITRC();
	return rc;
}


/*
 *  Returns Plugin specific information ...
 */
static int fs_get_plugin_info( char * descriptor_name, extended_info_array_t * * info )
{
	int                      rc = EINVAL;
	extended_info_array_t   *Info;
	extended_info_t		*iptr;
	char                     version_string[64];
#if (EVMS_ABI_CODE == 100)
	char                     required_version_string[64];
#else
	char                     required_engine_api_version_string[64];
	char                     required_fsim_api_version_string[64];
#endif	

	LOGENTRY();

	if (info) {

		if (descriptor_name == NULL) {
			*info = NULL;	  /* init to no info returned */

			Info = EngFncs->engine_alloc( sizeof(extended_info_array_t) + (9*sizeof(extended_info_t))  );
			if (Info) {

				Info->count = 0;

				sprintf(version_string, "%d.%d.%d",
					MAJOR_VERSION,
					MINOR_VERSION,
					PATCH_LEVEL );

#if (EVMS_ABI_CODE == 100)
				sprintf(required_version_string, "%d.%d.%d",
					pMyPluginRecord->required_api_version.major,
					pMyPluginRecord->required_api_version.minor,
					pMyPluginRecord->required_api_version.patchlevel );
#else
				sprintf(required_engine_api_version_string, "%d.%d.%d",
					pMyPluginRecord->required_engine_api_version.major,
					pMyPluginRecord->required_engine_api_version.minor,
					pMyPluginRecord->required_engine_api_version.patchlevel );

				sprintf(required_fsim_api_version_string, "%d.%d.%d",
					pMyPluginRecord->required_plugin_api_version.fsim.major,
					pMyPluginRecord->required_plugin_api_version.fsim.minor,
					pMyPluginRecord->required_plugin_api_version.fsim.patchlevel );
#endif

				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Short Name" );
				SET_STRING_FIELD( iptr->title, "Short Name" );
				SET_STRING_FIELD( iptr->desc, "A short name given to this plugin.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, pMyPluginRecord->short_name );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));

				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Long Name" );
				SET_STRING_FIELD( iptr->title, "Long Name" );
				SET_STRING_FIELD( iptr->desc, "A long name given to this plugin.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, pMyPluginRecord->long_name );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));

				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Type" );
				SET_STRING_FIELD( iptr->title, "Plugin Type" );
				SET_STRING_FIELD( iptr->desc, "There are various types of plugins; each responsible for some kind of storage object.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, "File System Interface Module" );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));

				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Version" );
				SET_STRING_FIELD( iptr->title, "Plugin Version" );
				SET_STRING_FIELD( iptr->desc, "This is the version number of the plugin.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, version_string );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));

#if (EVMS_ABI_CODE == 100)
				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Required Version" );
				SET_STRING_FIELD( iptr->title, "Required Engine Version" );
				SET_STRING_FIELD( iptr->desc, "This is the version of the engine that the plugin requires. It will not run on older versions of the Engine.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, required_version_string );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));
#else
				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Required Engine Services Version" );
				SET_STRING_FIELD( iptr->title, "Required Engine Services Version" );
				SET_STRING_FIELD( iptr->desc, "This is the version of the Engine services that this plug-in requires. It will not run on older versions of the Engine services.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, required_engine_api_version_string );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));

				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "Required Engine FSIM API Version" );
				SET_STRING_FIELD( iptr->title, "Required Engine FSIM API Version" );
				SET_STRING_FIELD( iptr->desc, "This is the version of the Engine FSIM API that this plug-in requires. It will not run on older versions of the Engine FSIM API.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, required_fsim_api_version_string );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));
#endif
				
#if defined(PACKAGE) && defined(VERSION)
				iptr = &Info->info[Info->count++];
				SET_STRING_FIELD( iptr->name, "E2fsprogs Version" );
				SET_STRING_FIELD( iptr->title, "E2fsprogs Version" );
				SET_STRING_FIELD( iptr->desc, "This is the version of the e2fsprogs that this plugin was shipped with.");
				iptr->type               = EVMS_Type_String;
				iptr->unit               = EVMS_Unit_None;
				SET_STRING_FIELD( iptr->value.s, VERSION );
				iptr->collection_type    = EVMS_Collection_None;
				memset( &iptr->group, 0, sizeof(group_info_t));
#endif
				
				*info = Info;

				rc = 0;
			} else {
				rc = ENOMEM;
			}

		} else {
			/* There is no more information on any of the descriptors. */
			rc = EINVAL;
		}
	}

	LOGEXITRC();
	return rc;
}


/*
 * How much can file system expand?
 */
static int fs_can_expand_by(logical_volume_t * volume, 
                            sector_count_t   * delta) 
{
	int  rc = 0;

	LOGENTRY();
	if (EVMS_IS_MOUNTED(volume)) {
		rc = EBUSY; /* If mounted, can't expand */
		goto errout;
	} 
	fs_get_fs_limits( volume,	/* reset limits */
			 &volume->min_fs_size,
			 &volume->max_fs_size,
			 &volume->max_vol_size);
	if (volume->fs_size + *delta > volume->max_fs_size) {
		*delta = volume->max_fs_size - volume->fs_size;
	}
errout:
	LOGEXITRC();
	return rc;
}


/*
 * How much can file system shrink?
 */
static int fs_can_shrink_by(logical_volume_t * volume, 
                            sector_count_t * delta) 
{
	int  rc = 0;

	LOGENTRY();
	if (EVMS_IS_MOUNTED(volume)) {
		rc = EBUSY; /* If mounted, can't shrink */
		goto errout;
	} 
	fs_get_fs_limits( volume,	/* reset limits */
			 &volume->min_fs_size,
			 &volume->max_fs_size,
			 &volume->max_vol_size);
	if (volume->fs_size - *delta < volume->min_fs_size) {
		*delta = volume->fs_size - volume->min_fs_size;
	}
	if (volume->min_fs_size >= volume->vol_size) {
		rc = ENOSPC;
	}
errout:
	LOGEXITRC();
	return rc;
}


/*-------------------------------------------------------------------------------------+
+                                                                                      +
+                                PLUGIN FUNCTION TABLE                                 +
+                                                                                      +
+--------------------------------------------------------------------------------------*/
static fsim_functions_t  fsim_ops = {

	setup_evms_plugin:  fs_setup,
	cleanup_evms_plugin:fs_cleanup,
	is_this_yours:      fs_probe,
	can_mkfs:           fs_can_mkfs,
	can_unmkfs:         fs_can_unmkfs,
	can_fsck:           fs_can_fsck,
	get_fs_size:        fs_get_fs_size,
	get_fs_limits:      fs_get_fs_limits,
	can_expand_by:      fs_can_expand_by,
	can_shrink_by:      fs_can_shrink_by,
	expand:             fs_expand,
	shrink:             fs_shrink,
	mkfs:               fs_mkfs,
	fsck:               fs_fsck,
	unmkfs:		        fs_unmkfs,
	get_option_count:   fs_get_option_count,
	init_task:          fs_init_task,
	set_option:         fs_set_option,
	set_volumes:        fs_set_volumes,
	get_volume_info:    fs_get_volume_info,
	get_plugin_info:    fs_get_plugin_info
};


/*-------------------------------------------------------------------------------------+
+                                                                                      +
+                         PLUGIN RECORD                                                +
+                                                                                      +
+-------------------------------------------------------------------------------------*/

plugin_record_t  ext2_plugrec = {
	id:                               SetPluginID(EVMS_OEM_IBM, EVMS_FILESYSTEM_INTERFACE_MODULE, FS_TYPE_EXT2 ),
	version:                          {MAJOR_VERSION, MINOR_VERSION, PATCH_LEVEL},
#if (EVMS_ABI_CODE == 100)
	required_api_version:             {ENGINE_PLUGIN_API_MAJOR_VERION, 
					   ENGINE_PLUGIN_API_MINOR_VERION,
					   ENGINE_PLUGIN_API_PATCH_LEVEL},
#else
	required_engine_api_version:      {ENGINE_SERVICES_API_MAJOR_VERION, 
					   ENGINE_SERVICES_API_MINOR_VERION,
					   ENGINE_SERVICES_API_PATCH_LEVEL},
	required_plugin_api_version: {fsim: {ENGINE_FSIM_API_MAJOR_VERION, 
					   ENGINE_FSIM_API_MINOR_VERION,
					   ENGINE_FSIM_API_PATCH_LEVEL} },
#endif
	short_name:                       "Ext2/3",
	long_name:                        "Ext2/3 File System Interface Module",
	oem_name:                         "IBM",
	functions:                        {fsim: &fsim_ops},
	container_functions:              NULL

};

