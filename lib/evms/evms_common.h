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
 * linux/include/linux/evms/evms_common.h
 *
 * EVMS common (kernel and user) header file
 *
 */

#ifndef __EVMS_COMMON_INCLUDED__
#define __EVMS_COMMON_INCLUDED__

/* version info */
#define EVMS_MAJOR                      63      /* use experimental major 63 for now */
#define EVMS_MAJOR_VERSION              1
#define EVMS_MINOR_VERSION              1
#define EVMS_PATCHLEVEL_VERSION         0

#define MAX_EVMS_VOLUMES                256 /* There are 256 minors */
#define EVMS_VOLUME_NAME_SIZE           127

#define IBM_OEM_ID                      8112    // could be anything, but used
                                                // I=8, B=1, M=12
// this one going away as well.
#define EVMS_OEM_IBM    IBM_OEM_ID

#define EVMS_INITIAL_CRC                0xFFFFFFFF
#define EVMS_MAGIC_CRC			0x31415926

#define EVMS_VSECTOR_SIZE               512
#define EVMS_VSECTOR_SIZE_SHIFT         9

#define DEV_PATH			"/dev"
#define EVMS_DIR_NAME			"evms"
#define EVMS_DEV_NAME			"block_device"
#define EVMS_DEV_NODE_PATH		DEV_PATH "/" EVMS_DIR_NAME "/"
#define EVMS_DEVICE_NAME		DEV_PATH "/" EVMS_DIR_NAME "/" EVMS_DEV_NAME

/* EVMS will always use 64-bit fields */
typedef u_int64_t evms_sector_t;

/* EVMS specific device handle type definition */
typedef u_int64_t evms_dev_handle_t;

typedef struct evms_version_s {
        /* major changes when incompatible differences are introduced */
        u_int32_t    major;
        /* minor changes when additions are made */
        u_int32_t    minor;
        /* patchlevel changes when bugs are fixed */
        u_int32_t    patchlevel;
} evms_version_t;

typedef enum evms_plugin_code_s {
        EVMS_NO_PLUGIN,                                // 0
        EVMS_DEVICE_MANAGER,                           // 1
        EVMS_SEGMENT_MANAGER,                          // 2
        EVMS_REGION_MANAGER,                           // 3
        EVMS_FEATURE,                                  // 4
        EVMS_ASSOCIATIVE_FEATURE,                      // 5
        EVMS_FILESYSTEM_INTERFACE_MODULE,              // 6
        EVMS_CLUSTER_MANAGER_INTERFACE_MODULE,         // 7
        EVMS_DISTRIBUTED_LOCK_MANAGER_INTERFACE_MODULE // 8
} evms_plugin_code_t;

#define SetPluginID(oem, type, id) ((oem << 16) | (type << 12) | id)
#define GetPluginOEM(pluginid) (pluginid >> 16)
#define GetPluginType(pluginid) ((pluginid >> 12) & 0xf)
#define GetPluginID(pluginid) (pluginid & 0xfff)

/* bit definitions for the flags field in
 * the EVMS LOGICAL NODE (kernel) and
 * the EVMS LOGICAL VOLUME (user) structures.
 */
#define EVMS_FLAGS_WIDTH                   	32
#define EVMS_VOLUME_FLAG                        (1<<0)
#define EVMS_VOLUME_PARTIAL_FLAG                (1<<1)
#define EVMS_VOLUME_PARTIAL			(1<<1)
#define EVMS_VOLUME_SET_READ_ONLY               (1<<2)
#define EVMS_VOLUME_READ_ONLY               	(1<<2)
/* queued flags bits */
#define EVMS_REQUESTED_DELETE			(1<<5)
#define EVMS_REQUESTED_QUIESCE			(1<<6)
#define EVMS_REQUESTED_VFS_QUIESCE		(1<<7)
/* this bit indicates corruption */
#define EVMS_VOLUME_CORRUPT			(1<<8)
/* these bits define the source of the corruption */
#define EVMS_VOLUME_SOFT_DELETED               	(1<<9)
#define EVMS_DEVICE_UNAVAILABLE			(1<<10)
/* these bits define volume status */
#define EVMS_MEDIA_CHANGED			(1<<20)
#define EVMS_DEVICE_UNPLUGGED			(1<<21)
/* these bits used for removable status */
#define EVMS_DEVICE_MEDIA_PRESENT		(1<<24)
#define EVMS_DEVICE_PRESENT			(1<<25)
#define EVMS_DEVICE_LOCKABLE			(1<<26)
#define EVMS_DEVICE_REMOVABLE			(1<<27)

/* version info for evms_feature_header_t */
#define EVMS_FEATURE_HEADER_MAJOR	3
#define EVMS_FEATURE_HEADER_MINOR	0
#define EVMS_FEATURE_HEADER_PATCHLEVEL	0

/* bit definitions of FEATURE HEADER bits in the FLAGS field  */
#define EVMS_FEATURE_ACTIVE                     (1<<0)
#define EVMS_FEATURE_VOLUME_COMPLETE            (1<<1)
/* bit definitions for VOLUME bits in the FLAGS field */
#define EVMS_VOLUME_DATA_OBJECT			(1<<16)
#define EVMS_VOLUME_DATA_STOP			(1<<17)

#define EVMS_FEATURE_HEADER_SIGNATURE           0x54414546 //FEAT
typedef struct evms_feature_header_s {
/*  0*/ u_int32_t               signature;
/*  4*/ u_int32_t               crc;
/*  8*/ evms_version_t          version;		/* structure version */
/* 20*/ evms_version_t          engine_version;		/* version of the Engine that */
							/* wrote this feature header  */
/* 32*/ u_int32_t               flags;
/* 36*/ u_int32_t               feature_id;
/* 40*/ u_int64_t		sequence_number;
/* 48*/ u_int64_t		alignment_padding;
        //required: starting lsn to 1st copy of feature's metadata.
/* 56*/ evms_sector_t           feature_data1_start_lsn;
/* 64*/	evms_sector_t		feature_data1_size; //in 512 byte units
	//optional: starting lsn to 2nd copy of feature's metadata.
	//          if unused set size field to 0.
/* 72*/ evms_sector_t           feature_data2_start_lsn;
/* 80*/	evms_sector_t		feature_data2_size; //in 512 byte units
/* 88*/ u_int64_t               volume_serial_number;
/* 96*/ u_int32_t               volume_system_id;       /* the minor is stored here */
/*100*/ u_int32_t               object_depth;	/* depth of object in the volume tree */
/*104*/ char                    object_name[EVMS_VOLUME_NAME_SIZE+1];
/*232*/ char                    volume_name[EVMS_VOLUME_NAME_SIZE+1];
/*360*/ unsigned char		pad[152];
/*512*/
} evms_feature_header_t;

/* EVMS specific error codes */
#define EVMS_FEATURE_FATAL_ERROR                257
#define EVMS_VOLUME_FATAL_ERROR                 258

#define EVMS_FEATURE_INCOMPLETE_ERROR		259

#endif
