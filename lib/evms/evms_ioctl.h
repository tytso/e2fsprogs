/* -*- linux-c -*- */
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
 */
/*
 * linux/include/linux/evms.h
 *
 * EVMS public kernel header file
 *
 */

#ifndef __EVMS_IOCTL_INCLUDED__
#define __EVMS_IOCTL_INCLUDED__

#include <linux/hdreg.h>

/* IOCTL interface version definitions */
#define EVMS_IOCTL_INTERFACE_MAJOR           10
#define EVMS_IOCTL_INTERFACE_MINOR           0
#define EVMS_IOCTL_INTERFACE_PATCHLEVEL      0

/* IOCTL definitions */
typedef enum evms_ioctl_cmds_s {
	/* version commands */
	EVMS_GET_IOCTL_VERSION_NUMBER = 0,
	EVMS_GET_VERSION_NUMBER,
#ifdef __KERNEL__
	/* EVMS internal commands */
	EVMS_GET_DISK_LIST_NUMBER = 0x40,
	EVMS_CHECK_MEDIA_CHANGE_NUMBER,
	EVMS_REVALIDATE_DISK_NUMBER,
	EVMS_OPEN_VOLUME_NUMBER,
	EVMS_CLOSE_VOLUME_NUMBER,
	EVMS_QUIESCE_VOLUME_NUMBER,
	EVMS_CHECK_DEVICE_STATUS_NUMBER,
#endif
	/* configuration commands */
	EVMS_GET_INFO_LEVEL_NUMBER = 0x80,
	EVMS_SET_INFO_LEVEL_NUMBER,
	EVMS_REDISCOVER_VOLUMES_NUMBER,
	EVMS_DELETE_VOLUME_NUMBER,
	EVMS_PLUGIN_IOCTL_NUMBER,
	EVMS_PROCESS_NOTIFY_EVENT_NUMBER,
	/* query info commands */
	EVMS_GET_LOGICAL_DISK_NUMBER = 0xC0,
	EVMS_GET_LOGICAL_DISK_INFO_NUMBER,
	EVMS_SECTOR_IO_NUMBER,
	EVMS_GET_MINOR_NUMBER,
	EVMS_GET_VOLUME_DATA_NUMBER,
	EVMS_GET_PLUGIN_NUMBER,
	EVMS_COMPUTE_CSUM_NUMBER,
	EVMS_GET_BMAP_NUMBER,
} evms_ioctl_cmds_t;

/* version commands */
#define EVMS_GET_IOCTL_VERSION_STRING   "EVMS_GET_IOCTL_VERSION"
#define EVMS_GET_IOCTL_VERSION          _IOR(EVMS_MAJOR, EVMS_GET_IOCTL_VERSION_NUMBER, evms_version_t)

#define EVMS_GET_VERSION_STRING         "EVMS_GET_VERSION"
#define EVMS_GET_VERSION                _IOR(EVMS_MAJOR, EVMS_GET_VERSION_NUMBER, evms_version_t)

#ifdef __KERNEL__

/* EVMS internal commands */
#define EVMS_GET_DISK_LIST_STRING       "EVMS_GET_DISK_LIST"
#define EVMS_GET_DISK_LIST              _IOWR(EVMS_MAJOR, EVMS_GET_DISK_LIST_NUMBER, evms_list_node_t **)

#define EVMS_CHECK_MEDIA_CHANGE_STRING  "EVMS_CHECK_MEDIA_CHANGE"
#define EVMS_CHECK_MEDIA_CHANGE         _IO(EVMS_MAJOR, EVMS_CHECK_MEDIA_CHANGE_NUMBER)

#define EVMS_REVALIDATE_DISK_STRING     "EVMS_REVALIDATE_DISK"
#define EVMS_REVALIDATE_DISK            _IO(EVMS_MAJOR, EVMS_REVALIDATE_DISK_NUMBER)

#define EVMS_OPEN_VOLUME_STRING         "EVMS_OPEN_VOLUME"
#define EVMS_OPEN_VOLUME                _IO(EVMS_MAJOR, EVMS_OPEN_VOLUME_NUMBER)

#define EVMS_CLOSE_VOLUME_STRING        "EVMS_CLOSE_VOLUME"
#define EVMS_CLOSE_VOLUME               _IO(EVMS_MAJOR, EVMS_CLOSE_VOLUME_NUMBER)

/* field: command: defines */
#define EVMS_UNQUIESCE          0
#define EVMS_QUIESCE            1

/* field: do_vfs: defines */
/* see evms_delete_volume */
typedef struct evms_quiesce_volume_s {
	int             command;		/* 0 = unquiesce, 1 = quiesce */
	int             minor;			/* minor device number of target volume */
	int             do_vfs;			/* 0 = do nothing, 1 = also perform equivalent VFS operation */
	int             status;			/* 0 = success */
} evms_quiesce_volume_t;

#define EVMS_QUIESCE_VOLUME_STRING      "EVMS_QUIESCE_VOLUME"
#define EVMS_QUIESCE_VOLUME             _IOR(EVMS_MAJOR, EVMS_QUIESCE_VOLUME_NUMBER, evms_quiesce_volume_t)

#define EVMS_CHECK_DEVICE_STATUS_STRING	"EVMS_CHECK_DEVICE_STATUS"
#define EVMS_CHECK_DEVICE_STATUS        _IOR(EVMS_MAJOR, EVMS_CHECK_DEVICE_STATUS_NUMBER, int)

#endif

/* configuration commands */
#define EVMS_GET_INFO_LEVEL_STRING      "EVMS_GET_INFO_LEVEL"
#define EVMS_GET_INFO_LEVEL             _IOR(EVMS_MAJOR, EVMS_GET_INFO_LEVEL_NUMBER, int)

#define EVMS_SET_INFO_LEVEL_STRING      "EVMS_SET_INFO_LEVEL"
#define EVMS_SET_INFO_LEVEL             _IOW(EVMS_MAJOR, EVMS_SET_INFO_LEVEL_NUMBER, int)

/* field: drive_count: defines */
#define REDISCOVER_ALL_DEVICES          0xFFFFFFFF
typedef struct evms_rediscover_s {
	int             status;
	unsigned int    drive_count;		/* 0xffffffff = rediscover all known disks */
	unsigned long  *drive_array;
} evms_rediscover_t;

#define EVMS_REDISCOVER_VOLUMES_STRING  "EVMS_REDISCOVER_VOLUMES"
#define EVMS_REDISCOVER_VOLUMES         _IOWR(EVMS_MAJOR, EVMS_REDISCOVER_VOLUMES_NUMBER, evms_rediscover_t)

/* field: command: defines */
#define EVMS_SOFT_DELETE        0
#define EVMS_HARD_DELETE        1

/* field: do_vfs: defines */
#define EVMS_VFS_DO_NOTHING     0
#define EVMS_VFS_DO             1
typedef struct evms_delete_volume_s {
	int             command;		/* 0 = "temp", 1 = "permanent" */
	int             minor;			/* minor device number of target volume */
	int             do_vfs;			/* 0 = do nothing, 1 = perform VFS operations */
	int             associative_minor;	/* optional minor of associative volume */
						/* must be 0 when not in use */
	int             status;			/* 0 = success, other is error */
} evms_delete_volume_t;

#define EVMS_DELETE_VOLUME_STRING       "EVMS_DELETE_VOLUME"
#define EVMS_DELETE_VOLUME              _IOR(EVMS_MAJOR, EVMS_DELETE_VOLUME_NUMBER, evms_delete_volume_t)

typedef struct evms_plugin_ioctl_s {
	unsigned long   feature_id;		/* ID of feature to receive this ioctl */
	int             feature_command;	/* feature specific ioctl command      */
	int             status;			/* 0 = completed, non-0 = error        */
	void           *feature_ioctl_data;	/* ptr to feature specific struct      */
} evms_plugin_ioctl_t;

#define EVMS_PLUGIN_IOCTL_STRING        "EVMS_PLUGIN_IOCTL"
#define EVMS_PLUGIN_IOCTL               _IOR(EVMS_MAJOR, EVMS_PLUGIN_IOCTL_NUMBER, evms_plugin_ioctl_t)

/* field: eventid: defines */
#define EVMS_EVENT_END_OF_DISCOVERY     0
typedef struct evms_event_s {
	int     pid;				/* PID to act on */
	int     eventid;			/* event id to respond to */
	int     signo;				/* signal # to send when event occurs */
} evms_event_t;

/* field: command: defines */
#define EVMS_EVENT_UNREGISTER   0
#define EVMS_EVENT_REGISTER     1
typedef struct evms_notify_s {
	int             command;		/* 0 = unregister, 1 = register */
	evms_event_t    eventry;		/* event structure */
	int             status;			/* return status */
} evms_notify_t;

#define EVMS_PROCESS_NOTIFY_EVENT_STRING "EVMS_PROCESS_NOTIFY_EVENT"
#define EVMS_PROCESS_NOTIFY_EVENT       _IOWR(EVMS_MAJOR, EVMS_PROCESS_NOTIFY_EVENT_NUMBER, evms_notify_t)

/* query info commands */

/* field: command: defines */
#define EVMS_FIRST_DISK         0
#define EVMS_NEXT_DISK          1

/* field: status: defines */
#define EVMS_DISK_INVALID       0
#define EVMS_DISK_VALID         1
typedef struct evms_user_disk_s {
	int             command;		/* 0 = first disk, 1 = next disk */
	int             status;			/* 0 = no more disks, 1 = valid disk info */
	unsigned long   disk_handle;		/* only valid when status == 1 */
} evms_user_disk_t;

#define EVMS_GET_LOGICAL_DISK_STRING    "EVMS_GET_LOGICAL_DISK"
#define EVMS_GET_LOGICAL_DISK           _IOWR(EVMS_MAJOR, EVMS_GET_LOGICAL_DISK_NUMBER, evms_user_disk_t)

/* flags fields described in evms_common.h */
typedef struct evms_user_disk_info_s {
	unsigned int    status;
	unsigned int    flags;
	unsigned long   disk_handle;
	unsigned int    disk_dev;
	struct hd_geometry geometry;
	unsigned int    block_size;
	unsigned int    hardsect_size;
	u_int64_t       total_sectors;
	char            disk_name[EVMS_VOLUME_NAME_SIZE];
} evms_user_disk_info_t;

#define EVMS_GET_LOGICAL_DISK_INFO_STRING "EVMS_GET_LOGICAL_DISK_INFO"
#define EVMS_GET_LOGICAL_DISK_INFO      _IOWR(EVMS_MAJOR, EVMS_GET_LOGICAL_DISK_INFO_NUMBER, evms_user_disk_info_t)

/* field: io_flag: defines */
#define EVMS_SECTOR_IO_READ	0
#define EVMS_SECTOR_IO_WRITE	1
typedef struct evms_sector_io_s {
	unsigned long   disk_handle;		/* valid disk handle */
	int             io_flag;		/* 0 = READ, 1 = WRITE */
	evms_sector_t   starting_sector;	/* disk relative LBA */
	evms_sector_t   sector_count;		/* number of sectors in IO */
	unsigned char  *buffer_address;		/* IO address */
	int             status;			/* 0 = success, not 0 = error */
} evms_sector_io_t;

#define EVMS_SECTOR_IO_STRING           "EVMS_SECTOR_IO"
#define EVMS_SECTOR_IO                  _IOWR(EVMS_MAJOR, EVMS_SECTOR_IO_NUMBER, evms_sector_io_t)

/* field: command: defines */
#define EVMS_FIRST_VOLUME       0
#define EVMS_NEXT_VOLUME        1

/* field: status: defines */
#define EVMS_VOLUME_INVALID     0
#define EVMS_VOLUME_VALID       1
typedef struct evms_user_minor_s {
	int             command;		/* 0 = first volume, 1 = next volume */
	int             status;			/* 0 = no more, 1 = valid info */
	int             minor;			/* only valid when status == 1 */
} evms_user_minor_t;

#define EVMS_GET_MINOR_STRING           "EVMS_GET_MINOR"
#define EVMS_GET_MINOR                  _IOWR(EVMS_MAJOR, EVMS_GET_MINOR_NUMBER, evms_user_minor_t)

/* flags field described in evms_common.h */
typedef struct evms_volume_data_s {
	int             minor;			/* minor of target volume */
	int             flags;
	char            volume_name[EVMS_VOLUME_NAME_SIZE + 1];
	int             status;
} evms_volume_data_t;

#define EVMS_GET_VOLUME_DATA_STRING     "EVMS_GET_VOLUME_DATA"
#define EVMS_GET_VOLUME_DATA            _IOWR(EVMS_MAJOR, EVMS_GET_VOLUME_DATA_NUMBER, evms_volume_data_t)

/* field: command: defines */
#define EVMS_FIRST_PLUGIN       0
#define EVMS_NEXT_PLUGIN        1

/* field: status: defines */
#define EVMS_PLUGIN_INVALID     0
#define EVMS_PLUGIN_VALID       1
typedef struct evms_kernel_plugin_s {
	int             command;		/* 0 = first item, 1 = next item */
	u_int32_t       id;			/* returned plugin id */
	evms_version_t  version;		/* maj,min,patch of plugin */
	int             status;			/* 0 = no more, 1 = valid info */
} evms_kernel_plugin_t;

#define EVMS_GET_PLUGIN_STRING          "EVMS_GET_PLUGIN"
#define EVMS_GET_PLUGIN                 _IOWR(EVMS_MAJOR, EVMS_GET_PLUGIN_NUMBER, evms_kernel_plugin_t)

typedef struct evms_compute_csum_s {
	unsigned char  *buffer_address;		/* IO address */
	int             buffer_size;		/* byte size of buffer */
	unsigned int    insum;			/* previous csum to be factored in */
	unsigned int    outsum;			/* resulting csum value of buffer */
	int             status;			/* 0 = success, not 0 = error */
} evms_compute_csum_t;

#define EVMS_COMPUTE_CSUM_STRING        "EVMS_COMPUTE_CSUM"
#define EVMS_COMPUTE_CSUM               _IOWR(EVMS_MAJOR, EVMS_COMPUTE_CSUM_NUMBER, evms_compute_csum_t)

typedef struct evms_get_bmap_s {
	u_int64_t       rsector;		/* input: volume relative rsector value */
						/* output: disk relative rsector value */
	u_int32_t       dev;			/* output = physical device */
	int             status;			/* 0 = success, not 0 = error */
} evms_get_bmap_t;

#define EVMS_GET_BMAP_STRING            "EVMS_GET_BMAP"
#define EVMS_GET_BMAP                   _IOWR(EVMS_MAJOR, EVMS_GET_BMAP_NUMBER, evms_get_bmap_t)

#endif
