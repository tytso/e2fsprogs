/*
 *
 *   Copyright (c) International Business Machines  Corp., 2001
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
 * Module: common.h
 */

#ifndef EVMS_COMMON_H_INCLUDED
#define EVMS_COMMON_H_INCLUDED 1

#include <linux/types.h>  /* will pull in platform specific data type info from linux/include/asm */

/* Need these to satisfy dependencies in evms_user.h
 * on systems running a 2.5.8 or newer kernel.
 */
typedef __u8	u8;
typedef __u16	u16;
typedef __u32	u32;
typedef __u64	u64;

#include <evms/evms_user.h>

/* Defines for storage object names */
#define EVMS_NAME_SIZE          EVMS_VOLUME_NAME_SIZE

/* Defines for the flags in the storage_object_t structure */
#define SOFLAG_DIRTY                (1<<0)
#define SOFLAG_NEW                  (1<<1)
#define SOFLAG_READ_ONLY            (1<<2)
#define SOFLAG_FEATURE_HEADER_DIRTY (1<<3)
#define SOFLAG_MUST_BE_TOP          (1<<4)
#define SOFLAG_IO_ERROR             (1<<5)
#define SOFLAG_CORRUPT              (1<<6)
#define SOFLAG_BIOS_READABLE        (1<<7)
#define SOFLAG_MUST_BE_VOLUME       (1<<8)
#define SOFLAG_NOT_CLAIMED          (1<<9)

/* Defines for flags in the storage_container_t structure */
#define SCFLAG_DIRTY                (1<<0)
#define SCFLAG_NEW                  (1<<1)

/* Defines for the flags in the logical_volume_t structure */
#define VOLFLAG_DIRTY               (1<<0)
#define VOLFLAG_NEW                 (1<<1)
#define VOLFLAG_READ_ONLY           (1<<2)
#define VOLFLAG_NEEDS_DEV_NODE      (1<<3)
#define VOLFLAG_COMPATIBILITY       (1<<4)
#define VOLFLAG_FOREIGN             (1<<5)
#define VOLFLAG_MKFS                (1<<6)
#define VOLFLAG_UNMKFS              (1<<7)
#define VOLFLAG_FSCK                (1<<8)
#define VOLFLAG_DEFRAG              (1<<9)
#define VOLFLAG_EXPAND_FS           (1<<10)
#define VOLFLAG_SHRINK_FS           (1<<11)
#define VOLFLAG_SYNC_FS             (1<<12)

/* A BOOLEAN variable is one which is either TRUE or FALSE. */
#ifndef BOOLEAN_DEFINED
  #define BOOLEAN_DEFINED 1
  typedef u_int8_t  BOOLEAN;
#endif

#ifndef TRUE
  #define TRUE  1
#endif
#ifndef FALSE
  #define FALSE 0
#endif

/*
 * Logical Sector Number:  This is a physical sector address on a
 * system drive.
 */
typedef u_int64_t       lsn_t;

/*
 * Logical Block Address:  This is a sector address on a volume which
 * will be translated to a Logical Sector Number.
 */
typedef u_int64_t       lba_t;

/*
 * A sector_count_t is a count of sectors.  It is mainly used to hold the size
 * of a disk, segment, region, etc.
 */
typedef u_int64_t       sector_count_t;

/*
 * A module_handle_t variable is one which holds a handle (or descriptor)
 * referencing a loaded module.
 */
typedef void          * module_handle_t;

/*
 * The standard data type for Engine handles.
 */
typedef u_int32_t       engine_handle_t;

/*
 * An object_handle_t holds a handle for an EVMS Engine object.
 */
typedef engine_handle_t object_handle_t;

/*
 * A plugin_handle_t holds a handle for an EVMS Engine plug-in.
 */
typedef engine_handle_t plugin_handle_t;

/*
 * A plugin_ID_t holds a unique ID for a plug-in.
 */
typedef u_int32_t       plugin_id_t;

/*
 * A plugin_type_t holds the type field of a plug-in's ID.
 */
typedef u_int8_t        plugin_type_t;

/*
 * The various modes in which the Engine can be.
 */
typedef enum {
    ENGINE_CLOSED = 0,
    ENGINE_READONLY,
    ENGINE_READWRITE
} engine_mode_t;

/*
 * The geometry of a disk, segment, region, etc.
 */
typedef struct geometry_s {
    u_int64_t   cylinders;
    u_int32_t   heads;
    u_int32_t   sectors_per_track;
    u_int32_t   bytes_per_sector;
    u_int64_t   boot_cylinder_limit;
    u_int64_t   block_size;
} geometry_t;

/*
 * The data types which a storage object can be.
 */
typedef enum {
    META_DATA_TYPE  = (1<<0),
    DATA_TYPE       = (1<<1),
    FREE_SPACE_TYPE = (1<<2)
} data_type_t;

/*
 * The types of structures the Engine exports
 */
typedef enum {
    PLUGIN      = (1<<0),
    DISK        = (1<<1),
    SEGMENT     = (1<<2),
    REGION      = (1<<3),
    EVMS_OBJECT = (1<<4),
    CONTAINER   = (1<<5),
    VOLUME      = (1<<6)
} object_type_t;

/*
 * Flags that can be used for filtering plug-ins on the evms_get_plugin_list API
 */
typedef enum {
    SUPPORTS_CONTAINERS = (1<<0)
} plugin_search_flags_t;

/*
 * Flags that can be used for filtering objects on the evms_get_object_list API
 */
typedef enum {
    TOPMOST =           (1<<0),
    NOT_MUST_BE_TOP =   (1<<1),
    WRITEABLE =         (1<<2)
} object_search_flags_t;

#define VALID_INPUT_OBJECT      (TOPMOST | NOT_MUST_BE_TOP | WRITEABLE)

/*
 * Debug levels
 * These levels should be kept in sync with the debug levels defined for the
 * EVMS kernel in linux/evms/evms.h.
 */
typedef enum {
    /*
     * Use CRITICAL for messages that indicate that the health of the
     * system/Engine is in jeopardy.  Something _really_ bad happened,
     * such as failure to allocate memory or control structures are
     * corrupted.
     */
    CRITICAL = 0,

    /*
     * Use SERIOUS for messages that something bad has happened, but not
     * as bad a CRITICAL.
     */
    SERIOUS = 1,

    /*
     * Use ERROR for messages that indicate the user caused an error,
     * such as passing a bad parameter.  The message should help the
     * user correct the problem.
     */
    ERROR = 2,

    /*
     * Use WARNING for messages that indicate that something is not quite
     * right and the user should know about it.  You may or may not be able
     * to work around the problem.
     */
    WARNING = 3,

    /*
     * Use DEFAULT for informational messages that do not indicate problems, or
     * that a problem occurred but there was a work-around.  DEFAULT messages
     * should be things that the user would usually want to know during any run
     * of the Engine, such as how many volumes were discovered on the system,
     * and not necessarily what a developer would want to know (use DETAILS or
     * DEBUG for that).  Since DEFAULT is the default debug level, be careful
     * not to put DEFAULT messages in loops or frequently executed code as they
     * will bloat the log file.
     */
    DEFAULT = 5,

    /*
     * Use DETAILS to provide more detailed information about the system.  The
     * message may provide additional information about the progress of the
     * system.  It may contain more information about a DEFAULT message or more
     * information about an error condition.
     */
    DETAILS = 6,

    /*
     * Use DEBUG for messages that would help debug a problem, such as tracing
     * code paths or dumping the contents of variables.
     */
    DEBUG = 7,

    /*
     * Use EXTRA to provided more information than your standard debug messages
     * provide.
     */

    EXTRA = 8,

    /*
     * Use ENTRY_EXIT to trace entries and exits from functions.
     */
    ENTRY_EXIT = 9,

    /*
     * Use EVERYTHING for all manner of verbose output.  Feel free to bloat the
     * log file with any messages that would help you debug a problem.
     */
    EVERYTHING = 10

} debug_level_t;


/*
 * Handy macros for finding the min and max of two numbers.
 */
#ifndef min
    #define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef max
    #define max(a,b) (((a)>(b))?(a):(b))
#endif


#endif

