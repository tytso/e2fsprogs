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

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

/* version info */
#define EVMS_MAJOR_VERSION              1
#define EVMS_MINOR_VERSION              2
#define EVMS_PATCHLEVEL_VERSION         0

#define EVMS_MAJOR			117

#define MAX_EVMS_VOLUMES                256     /* There are 256 minors */
#define EVMS_VOLUME_NAME_SIZE           127

#define IBM_OEM_ID                      8112    // could be anything, but used

// I=8, B=1, M=12
// this one going away as well.
#define EVMS_OEM_IBM                    IBM_OEM_ID

#define EVMS_INITIAL_CRC                0xFFFFFFFF
#define EVMS_MAGIC_CRC                  0x31415926

#define EVMS_VSECTOR_SIZE               512
#define EVMS_VSECTOR_SIZE_SHIFT         9

#define DEV_PATH                        "/dev"
#define EVMS_DIR_NAME                   "evms"
#define EVMS_DEV_NAME                   "block_device"
#define EVMS_DEV_NODE_PATH              DEV_PATH "/" EVMS_DIR_NAME "/"
#define EVMS_DEVICE_NAME                DEV_PATH "/" EVMS_DIR_NAME "/" EVMS_DEV_NAME

/* EVMS will always use 64-bit fields */
typedef u_int64_t evms_sector_t;

/* EVMS specific device handle type definition */
typedef u_int64_t evms_dev_handle_t;

typedef struct evms_version {
    /* major changes when incompatible differences are introduced */
    u_int32_t major;
    /* minor changes when additions are made */
    u_int32_t minor;
    /* patchlevel changes when bugs are fixed */
    u_int32_t patchlevel;
} evms_version_t;

typedef enum evms_plugin_code {
    EVMS_NO_PLUGIN,                                     // 0
    EVMS_DEVICE_MANAGER,                                // 1
    EVMS_SEGMENT_MANAGER,                               // 2
    EVMS_REGION_MANAGER,                                // 3
    EVMS_FEATURE,                                       // 4
    EVMS_ASSOCIATIVE_FEATURE,                           // 5
    EVMS_FILESYSTEM_INTERFACE_MODULE,                   // 6
    EVMS_CLUSTER_MANAGER_INTERFACE_MODULE,              // 7
    EVMS_DISTRIBUTED_LOCK_MANAGER_INTERFACE_MODULE      // 8
} evms_plugin_code_t;

#define SetPluginID(oem, type, id) ((oem << 16) | (type << 12) | id)
#define GetPluginOEM(pluginid) (pluginid >> 16)
#define GetPluginType(pluginid) ((pluginid >> 12) & 0xf)
#define GetPluginID(pluginid) (pluginid & 0xfff)

/* bit definitions for the flags field in
 * the EVMS LOGICAL NODE (kernel) and
 * the EVMS LOGICAL VOLUME (user) structures.
 */
#define EVMS_FLAGS_WIDTH                32
#define EVMS_VOLUME_FLAG                (1<<0)
#define EVMS_VOLUME_PARTIAL_FLAG        (1<<1)
#define EVMS_VOLUME_PARTIAL             (1<<1)
#define EVMS_VOLUME_SET_READ_ONLY       (1<<2)
#define EVMS_VOLUME_READ_ONLY           (1<<2)

/* queued flags bits */
#define EVMS_REQUESTED_DELETE           (1<<5)
#define EVMS_REQUESTED_QUIESCE          (1<<6)
#define EVMS_REQUESTED_VFS_QUIESCE      (1<<7)

/* this bit indicates corruption */
#define EVMS_VOLUME_CORRUPT             (1<<8)

/* these bits define the source of the corruption */
#define EVMS_VOLUME_SOFT_DELETED        (1<<9)
#define EVMS_DEVICE_UNAVAILABLE         (1<<10)

/* these bits are used for moving objects. */
#define EVMS_MOVE_PARENT                (1<<11)

/* these bits define volume status */
#define EVMS_MEDIA_CHANGED              (1<<20)
#define EVMS_DEVICE_UNPLUGGED           (1<<21)

/* these bits used for removable status */
#define EVMS_DEVICE_MEDIA_PRESENT       (1<<24)
#define EVMS_DEVICE_PRESENT             (1<<25)
#define EVMS_DEVICE_LOCKABLE            (1<<26)
#define EVMS_DEVICE_REMOVABLE           (1<<27)

/* version info for evms_feature_header_t */
#define EVMS_FEATURE_HEADER_MAJOR       3
#define EVMS_FEATURE_HEADER_MINOR       0
#define EVMS_FEATURE_HEADER_PATCHLEVEL  0

/* version info for evms_feature_header_t that has fields for move*/
#define EVMS_MOVE_FEATURE_HEADER_MAJOR       3
#define EVMS_MOVE_FEATURE_HEADER_MINOR       1
#define EVMS_MOVE_FEATURE_HEADER_PATCHLEVEL  0

/* bit definitions of FEATURE HEADER bits in the FLAGS field  */
#define EVMS_FEATURE_ACTIVE             (1<<0)
#define EVMS_FEATURE_VOLUME_COMPLETE    (1<<1)

/* bit definitions for VOLUME bits in the FLAGS field */
#define EVMS_VOLUME_DATA_OBJECT         (1<<16)
#define EVMS_VOLUME_DATA_STOP           (1<<17)

#define EVMS_FEATURE_HEADER_SIGNATURE   0x54414546  // "FEAT"
typedef struct evms_feature_header {
/*  0*/
    u_int32_t signature;
/*  4*/ u_int32_t crc;
/*  8*/ evms_version_t version;
    /* structure version */
/* 20*/ evms_version_t engine_version;
    /* version of the Engine that */
    /* wrote this feature header  */
/* 32*/ u_int32_t flags;
/* 36*/ u_int32_t feature_id;
/* 40*/ u_int64_t sequence_number;
/* 48*/ u_int64_t alignment_padding;
    //required: starting lsn to 1st copy of feature's metadata.
/* 56*/ evms_sector_t feature_data1_start_lsn;
/* 64*/ evms_sector_t feature_data1_size;
    //in 512 byte units
    //optional: starting lsn to 2nd copy of feature's metadata.
    //          if unused set size field to 0.
/* 72*/ evms_sector_t feature_data2_start_lsn;
/* 80*/ evms_sector_t feature_data2_size;
    //in 512 byte units
/* 88*/ u_int64_t volume_serial_number;
/* 96*/ u_int32_t volume_system_id;
    /* the minor is stored here */
/*100*/ u_int32_t object_depth;
    /* depth of object in the volume tree */
/*104*/ char object_name[EVMS_VOLUME_NAME_SIZE + 1];
/*232*/ char volume_name[EVMS_VOLUME_NAME_SIZE + 1];
/*360*/ u_int32_t move_source;
    /* version 3.1.0 feature header */
/*364*/ u_int32_t move_target;
    /* version 3.1.0 feature header */
/*368*/ unsigned char pad[144];
/*512*/
} evms_feature_header_t;

/* EVMS specific error codes */
#define EVMS_FEATURE_FATAL_ERROR        257
#define EVMS_VOLUME_FATAL_ERROR         258

#define EVMS_FEATURE_INCOMPLETE_ERROR   259

/* Defines for storage object names */
#define EVMS_NAME_SIZE                  EVMS_VOLUME_NAME_SIZE

/* Defines for the flags in the storage_object_t structure */
#define SOFLAG_DIRTY                    (1<<0)
#define SOFLAG_NEW                      (1<<1)
#define SOFLAG_READ_ONLY                (1<<2)
#define SOFLAG_FEATURE_HEADER_DIRTY     (1<<3)
#define SOFLAG_MUST_BE_TOP              (1<<4)
#define SOFLAG_IO_ERROR                 (1<<5)
#define SOFLAG_CORRUPT                  (1<<6)
#define SOFLAG_BIOS_READABLE            (1<<7)
#define SOFLAG_MUST_BE_VOLUME           (1<<8)
#define SOFLAG_NOT_CLAIMED              (1<<9)
#define SOFLAG_HAS_STOP_DATA            (1<<10)

/* Defines for flags in the storage_container_t structure */
#define SCFLAG_DIRTY                    (1<<0)
#define SCFLAG_NEW                      (1<<1)

/* Defines for the flags in the logical_volume_t structure */
#define VOLFLAG_DIRTY                   (1<<0)
#define VOLFLAG_NEW                     (1<<1)
#define VOLFLAG_READ_ONLY               (1<<2)
#define VOLFLAG_NEEDS_DEV_NODE          (1<<3)
#define VOLFLAG_COMPATIBILITY           (1<<4)
#define VOLFLAG_FOREIGN                 (1<<5)
#define VOLFLAG_MKFS                    (1<<6)
#define VOLFLAG_UNMKFS                  (1<<7)
#define VOLFLAG_FSCK                    (1<<8)
#define VOLFLAG_DEFRAG                  (1<<9)
#define VOLFLAG_EXPAND_FS               (1<<10)
#define VOLFLAG_SHRINK_FS               (1<<11)
#define VOLFLAG_SYNC_FS                 (1<<12)
#define VOLFLAG_PROBE_FS                (1<<13)
#define VOLFLAG_IS_EXTERNAL_LOG         (1<<14)
#define VOLFLAG_HAS_EXTERNAL_LOG        (1<<15)

/* A BOOLEAN variable is one which is either TRUE or FALSE. */
#ifndef BOOLEAN_DEFINED
  #define BOOLEAN_DEFINED 1
typedef unsigned char BOOLEAN;
#endif

#ifndef TRUE
  #define TRUE  1
#endif
#ifndef FALSE
  #define FALSE 0
#endif

/*
 * Logical Sector Number: This is a physical sector address on a system drive.
 */
typedef u_int64_t       lsn_t;

/*
 * Logical Block Address: This is a sector address on a volume which will be
 * translated to a Logical Sector Number.
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
 * The standard data type for Engine handles
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
 * The various modes in which the Engine can be
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
 * Definitions and structures for progress indicators.
 */
typedef enum {
    DISPLAY_PERCENT = 0,    /* Display the progress as a percentage.      */
    /* This is the default display mode.          */
    DISPLAY_COUNT,          /* Display the progress as a count.           */
    INDETERMINATE           /* Progress cannot be measured with a count   */
    /* of items.  Progress is simply "working".   */
} progress_type_t;

typedef struct progress_s {
    /*
     * The plug-in MUST set id to zero on the first call.  An id of zero
     * tells the UI to start a new progress indicator.  The UI MUST set the
     * id field to a nonzero number that is unique from any other progress
     * indicators that may be in effect.
     */
    uint            id;

    /* Short title for the progress indicator */
    char          * title;

    /* Longer description of the task that is in progress */
    char          * description;

    /* Type of progress indicator */
    progress_type_t type;

    /*
     * Current number of items completed.  The plug-in should set count to
     * zero on the first call.
     */
    uint            count;

    /*
     * Total number of items to be completed.  The UI uses count/total_count
     * to calculate the percent complete.  On the plug-in's last call to
     * update the progress it MUST set count >= total_count.  When the UI
     * gets a call for progress and count >= total_count, it knows it is the
     * last call and closes the progress indicator.
     */
    uint            total_count;

    /*
     * The plug-in may provide an estimate of how many seconds it will take
     * to complete the operation, but it is not required.  If the plug-in is
     * not providing a time estimate it MUST set remaining_seconds to zero.
     *
     * The plug-in may update remaining_seconds on subsequent calls for
     * progress.  If the plug-in does not provide a time estimate, the UI
     * may provide one based on the time elapsed between the calls to update
     * the progress and the numbers in the count and total_count fields.
     */
    uint            remaining_seconds;

    /*
     * A place for the plug-in to store any data relating to this progress
     * indicator.
     */
    void          * plugin_private_data;

    /*
     * A place for the UI to store any data relating to this progress
     * indicator.
     */
    void          * ui_private_data;
} progress_t;

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
     * Use DEFAULT for informational messages that do not indicate problems,
     * or that a problem occurred but there was a work-around.  DEFAULT
     * messages should be things that the user would usually want to know
     * during any run of the Engine, such as how many volumes were discovered
     * on the system, and not necessarily what a developer would want to know
     * (use DETAILS or DEBUG for that).  Since DEFAULT is the default debug
     * level, be careful not to put DEFAULT messages in loops or frequently
     * executed code as they will bloat the log file.
     */
    DEFAULT = 5,

    /*
     * Use DETAILS to provide more detailed information about the system.
     * The message may provide additional information about the progress of
     * the system.  It may contain more information about a DEFAULT message
     * or more information about an error condition.
     */
    DETAILS = 6,

    /*
     * Use DEBUG for messages that would help debug a problem, such as
     * tracing code paths or dumping the contents of variables.
     */
    DEBUG = 7,

    /*
     * Use EXTRA to provided more information than your standard debug
     * messages provide.
     */

    EXTRA = 8,

    /*
     * Use ENTRY_EXIT to trace entries and exits from functions.
     */
    ENTRY_EXIT = 9,

    /*
     * Use EVERYTHING for all manner of verbose output.  Feel free to bloat
     * the log file with any messages that would help you debug a problem.
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

