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
 * Module: enginestructs.h
 */

#ifndef EVMS_ENGINESTRUCTS_H_INCLUDED
#define EVMS_ENGINESTRUCTS_H_INCLUDED 1

#include <byteswap.h>
#include <dlist.h>
#include <common.h>
#include <options.h>

struct plugin_functions_s;
struct fsim_functions_s;
struct container_functions_s;

/*
 * The so_record_t contains information about a .so that was loaded which
 * contains plug-in(s).
 */
typedef struct so_record_s {
    char          * name;
    module_handle_t handle;
    dlist_t         plugin_list;
} so_record_t;


typedef struct plugin_record_s {
    object_handle_t                 app_handle;            /* External API handle for this structure; */
                                                           /* used only by the Engine */
    plugin_id_t                     id;                    /* Plug-in's ID */
    evms_version_t                  version;               /* Plug-in's version */
#if (EVMS_ABI_CODE == 100)
    evms_version_t                  required_api_version;  /* Version of the Engine plug-in API */
                                                           /* that the plug-in requires */
#else
    evms_version_t                  required_engine_api_version;
                                                           /* Version of the Engine services API */
                                                           /* that the plug-in requires */
    union {
        evms_version_t              plugin;                /* Version of the Engine plug-in API */
                                                           /* that the plug-in requires */
        evms_version_t              fsim;                  /* Version of the Engine FSIM API */
                                                           /* that the FSIM plug-in requires */
    } required_plugin_api_version;
    evms_version_t                  required_container_api_version;
                                                           /* Version of the Engine container API */
                                                           /* that the plug-in requires */
#endif
    so_record_t                   * so_record;             /* Record for the shared object from */
                                                           /* which the plug-in was loaded */
    char                          * short_name;
    char                          * long_name;
    char                          * oem_name;
    union {
        struct plugin_functions_s * plugin;
        struct fsim_functions_s   * fsim;
    } functions;
    struct container_functions_s  * container_functions;   /* Optional container functions if the */
                                                           /* plug-in supports containers */
} plugin_record_t;


typedef struct storage_object_s {
    object_handle_t              app_handle;            /* External API handle for this structure; */
                                                        /* used only by the Engine */
    object_type_t                object_type;           /* SEGMENT, REGION, DISK ,... */
    data_type_t                  data_type;             /* DATA_TYPE, META_DATA_TYPE, FREE_SPACE_TYPE */
    plugin_record_t            * plugin;                /* Plug-in record of plug-in that manages this object */
    struct storage_container_s * producing_container;   /* storage_container that produced this object */
    struct storage_container_s * consuming_container;   /* storage_container that consumed this object */
    dlist_t                      parent_objects;        /* List of parent objects, filled in by parent */
    dlist_t                      child_objects;         /* List of child objects, filled in by owner */
    struct storage_object_s    * associated_object;     /* Object to which this object is associated */
    u_int32_t                    flags;                 /* Defined by SOFLAG_???? in common.h */
    lsn_t                        start;                 /* Relative starting sector of this object */
    sector_count_t               size;                  /* Size of object in sectors */
    struct logical_volume_s    * volume;                /* Volume which comprises this object */
    evms_feature_header_t      * feature_header;        /* Copy of EVMS storage object's top feature header */
                                                        /* read in by Engine */
                                                        /* NULL if it does not exist */
    geometry_t                   geometry;              /* Optional geometry of the object */
    void                       * private_data;          /* Optional plug-in's data for the object */
    void                       * consuming_private_data;/* Optional consuming plug-in's data for the object */
    char                         name[EVMS_NAME_SIZE+1];/* Object's name, filled in by owner */
} storage_object_t;


typedef struct storage_container_s {
    object_handle_t   app_handle;               /* External API handle for this structure; */
                                                /* used only by the Engine */
    plugin_record_t * plugin;                   /* Plug-in record of the plug-in that manages */
                                                /* this container */
                                                /* Filled in by the plug-in during discover */
                                                /* or create_container() */
    uint              flags;                    /* Defined by SCFLAG_???? in common.h */
    dlist_t           objects_consumed;         /* List of objects in this container */
                                                /* The Engine allocate_container API will create the */
                                                /* dlist_t anchor for this list. */
                                                /* The plug-in inserts storage_object_t structures */
                                                /* into this list when it assigns objects to this */
                                                /* container. */
    dlist_t           objects_produced;         /* List of objects produced from this container, */
                                                /* including free space objects */
                                                /* The Engine allocate_container API will create the */
                                                /* dlist_t anchor for this list. */
                                                /* The plug-in inserts storage_object_t structures */
                                                /* into this list when it produces objects from this */
                                                /* container. */
    sector_count_t    size;                     /* Total size of all objects on the objects_produced list */
    void            * private_data;             /* Optional plug-in data for the container */
    char              name[EVMS_NAME_SIZE+1];   /* Container name, filled in by the plug-in */
} storage_container_t;


/*
 * The logical_volume structures are created and managed by the Engine.
 */
typedef struct logical_volume_s {
    object_handle_t           app_handle;           /* External API handle for this structure; */
                                                    /* used only by the Engine */
    plugin_record_t         * file_system_manager;  /* Plug-in record of the File System Interface */
                                                    /* Module that handles this volume */
    plugin_record_t         * original_fsim;        /* Plug-in record of the File System Interface */
                                                    /* Module that was initially discovered for this volume */
    char                    * mount_point;          /* Dir where the volume is mounted, NULL if not mounted */
    sector_count_t            fs_size;              /* Size of the file system */
    sector_count_t            min_fs_size;          /* Minimum size for the file system */
    sector_count_t            max_fs_size;          /* Maximum size for the file system */
    sector_count_t            original_vol_size;    /* Size of the file system before expand or shrink */
    sector_count_t            vol_size;             /* Size of the volume */
    sector_count_t            max_vol_size;         /* Maximum size for the volume */
#if (EVMS_ABI_CODE >= 110)
    sector_count_t            shrink_vol_size;      /* Size to which to shrink the volume */
#endif
    struct logical_volume_s * associated_volume;    /* Volume to which this volume is associated */
                                                    /* by an associative feature */
    option_array_t          * mkfs_options;         /* Options for mkfs */
    option_array_t          * fsck_options;         /* Options for fsck */
    option_array_t          * defrag_options;       /* Options for defrag */
    storage_object_t        * object;               /* Top level storage_object_t for the volume */
    uint                      minor_number;         /* Volume's minor number */
    u_int64_t                 serial_number;        /* Volume's serial number */
    u_int32_t                 flags;                /* Defined by VOLFLAG_???? defines */
    void                    * private_data;         /* Private data pointer for FSIMs. */
#if (EVMS_ABI_CODE >= 110)
    void                    * original_fsim_private_data;
                                                    /* Private data of original FSIM. */
#endif
    char                      name[EVMS_VOLUME_NAME_SIZE+1];
                                                    /* Volume name, filled in by the Engine */
#if (EVMS_ABI_CODE >= 110)
    char                      dev_node[EVMS_VOLUME_NAME_SIZE+1];
                                                    /* Device node */
#endif
} logical_volume_t;


/*
 * Structure for a declined object.  Includes a pointer to the declined object
 * and a reason (usually an error code).
 */
typedef struct declined_object_s {
    storage_object_t * object;
    int                reason;
} declined_object_t;


/*
 * Tags for objects in dlists
 */
typedef enum {
    PLUGIN_TAG          = PLUGIN,
    DISK_TAG            = DISK,
    SEGMENT_TAG         = SEGMENT,
    REGION_TAG          = REGION,
    EVMS_OBJECT_TAG     = EVMS_OBJECT,
    CONTAINER_TAG       = CONTAINER,
    VOLUME_TAG          = VOLUME,
    DECLINED_OBJECT_TAG = (1<<7),
    VOLUME_DATA_TAG     = (1<<8),
    TASK_TAG            = (1<<9),
    KILL_SECTOR_TAG     = (1<<10),
    BLOCK_RUN_TAG       = (1<<11),
    EXPAND_OBJECT_TAG   = (1<<12),
    SHRINK_OBJECT_TAG   = (1<<13)
} dlist_tag_t;


typedef struct chs_s {
    u_int32_t cylinder;
    u_int32_t head;
    u_int32_t sector;
} chs_t;

/*
 * The block_run_t is used to describe a run of contiguous physical sectors on
 * a disk.
 */
typedef struct block_run_s {
    storage_object_t * disk;
    lba_t              lba;
    u_int64_t          number_of_blocks;
} block_run_t;

/*
 * The kill_sector_record_t structure records a run of contiguous physical
 * sectors on a disk that are to be zeroed out as part of the committing of
 * changes to the disk.  Kill sectors are used to wipe data off of the disk
 * so that it will not be found on a rediscover.
 */
typedef struct kill_sector_record_s {
    storage_object_t * logical_disk;
    lsn_t              sector_offset;
    sector_count_t     sector_count;
} kill_sector_record_t;

/*
 * The expand_object_info_t structure contains information about an object
 * that is a candidate for expanding.  It contains a pointer to the object
 * and the maximum delta size by which the object can expand.
 */
typedef struct expand_object_info_s {
    storage_object_t * object;
    sector_count_t     max_expand_size;
} expand_object_info_t;

/*
 * The shrink_object_info_t structure contains information about an object
 * that is a candidate for shrinking.  It contains a pointer to the object
 * and the maximum delta size by which the object can shrink.
 */
typedef struct shrink_object_info_s {
    storage_object_t * object;
    sector_count_t     max_shrink_size;
} shrink_object_info_t;

/*
 * Option descriptor structure
 */
typedef struct option_desc_array_s {
    u_int32_t           count;                  /* Number of option descriptors in the following array */
    option_descriptor_t option[1];              /* option_descriptor_t is defined in option.h */
} option_desc_array_t;


/*
 * Task context structure
 */
typedef struct task_context_s {
    plugin_record_t     * plugin;               /* Plug-in being communicated with */
    storage_object_t    * object;               /* Object upon which to do the action */
    storage_container_t * container;            /* Container upon which to do the action */
    logical_volume_t    * volume;               /* Volume upon which to do the action */
    task_action_t         action;               /* API application is interested in calling */
    option_desc_array_t * option_descriptors;   /* Array of current task option descriptors */
    dlist_t               acceptable_objects;   /* Current list of acceptable parameters */
    dlist_t               selected_objects;     /* Current list of selected parameters */
    u_int32_t             min_selected_objects; /* Minimum number of objects that must be selected. */
    u_int32_t             max_selected_objects; /* Maximum number of objects that can be selected. */
} task_context_t;


/* Enum for the phases of the commit process. */
typedef enum {
    SETUP = 0,
    FIRST_METADATA_WRITE = 1,
    SECOND_METADATA_WRITE = 2,
    POST_REDISCOVER = 3
} commit_phase_t;


/*
 * Macros for referencing fields in disk structures.
 * EVMS writes all disk structures in little endian format.  These macros can
 * be used to access the fields of structures on disk regardless of the
 * endianness of the CPU architecture.
 */

#if __BYTE_ORDER == __BIG_ENDIAN
#define CPU_TO_DISK16(x)    (bswap_16(x))
#define CPU_TO_DISK32(x)    (bswap_32(x))
#define CPU_TO_DISK64(x)    (bswap_64(x))

#define DISK_TO_CPU16(x)    (bswap_16(x))
#define DISK_TO_CPU32(x)    (bswap_32(x))
#define DISK_TO_CPU64(x)    (bswap_64(x))

#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define CPU_TO_DISK16(x)    (x)
#define CPU_TO_DISK32(x)    (x)
#define CPU_TO_DISK64(x)    (x)

#define DISK_TO_CPU16(x)    (x)
#define DISK_TO_CPU32(x)    (x)
#define DISK_TO_CPU64(x)    (x)

#else
#error "__BYTE_ORDER must be defined as __LITTLE_ENDIAN or __BIG_ENDIAN"

#endif

#endif
