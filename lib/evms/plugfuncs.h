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
 * Module: plugfuncs.h
 */

#ifndef EVMS_PLUGFUNCS_H_INCLUDED
#define EVMS_PLUGFUNCS_H_INCLUDED 1

#include <dlist.h>
#include <common.h>
#include <options.h>
#include <enginestructs.h>


/* Maximum length of a user message. */
#define MAX_USER_MESSAGE_LEN    10240


#if ((EVMS_ABI_CODE == 110) || (EVMS_ABI_CODE == 120))
#define ENGINE_SERVICES_API_MAJOR_VERION  8
#if (EVMS_ABI_CODE == 110)
#define ENGINE_SERVICES_API_MINOR_VERION  0
#else
#define ENGINE_SERVICES_API_MINOR_VERION  1
#endif
#define ENGINE_SERVICES_API_PATCH_LEVEL   0
#endif

/*
 * For all can_????() functions, the function returns 0 if "yes", else a reason code.
 */

typedef struct engine_functions_s {

#if (EVMS_ABI_CODE >= 110)
    /*
     * Get the version of the plug-in API that this Engine provides.
     */
    void (*get_engine_plugin_api_version)(evms_version_t * version);
#endif

    /*
     * Get a list of the user space plug-ins that are loaded, optionally
     * filtering by type and flags.  If the type parameter is not 0, only
     * plug-ins of that type will be returned.  If type is 0, all plug-ins will
     * be returned.  See common.h for the definitions of plugin_search_flags_t.
     */
    int (*get_plugin_list)(plugin_type_t         type,
                           plugin_search_flags_t flags,
                           dlist_t             * plugins);

    /*
     * Get the plugin_record_t for a given plug-in ID.
     */
    int (*get_plugin_by_ID)(plugin_id_t         plugin_id,
                            plugin_record_t * * plugin);

    /*
     * Get the plugin_record_t for a given plug-in short name.
     */
    int (*get_plugin_by_name)(char              * plugin_short_name,
                              plugin_record_t * * plugin);

    /*
     * Get a list of volumes, optionally filtering by FSIM.  If FSIM is
     * specified, only volumes managed by that FSIM will be returned.  If FSIM
     * is NULL, all volumes will be returned.
     */
    int (*get_volume_list)(plugin_record_t * fsim,
                           dlist_t         * volume_list);

    /*
     * Get a list of objects, optionally filtering by object type, data type,
     * and plug-in.  See the object_type_t, data_type_t, and
     * object_search_flags_t enums in common.h.  If object_type is 0, objects of
     * any type will be returned.  If data_type is 0, objects of any data type
     * will be returned.  If plugin is NULL, objects managed by any plug-in will
     * be returned.
     */
    int (*get_object_list)(object_type_t         object_type,
                           data_type_t           data_type,
                           plugin_record_t     * plugin,
                           object_search_flags_t flags,
                           dlist_t             * objects);

    /*
     * Get a list of storage containers, optionally filtering by plug-in.
     * If plugin is specified, only containers managed by that plug-in
     * will be returned.  If plugin is NULL, all containers will be returned.
     */
    int (*get_container_list)(plugin_record_t * plugin,
                              dlist_t *         container_list);

    /*
     * Issue an ioctl to the EVMS kernel block device.  The Engine opens
     * and locks the EVMS kernel block device.  While the Engine is open
     * for writing, no other application, not even Engine plug-ins, can
     * open the EVMS kernel block device.  Plug-ins use this service
     * to have the Engine issue an ioctl to the EVMS kernel block device
     * on their behalf.
     */
    int (*ioctl_evms_kernel)(unsigned long cmd,
                             void * arg);
    /*
     * Allocate a storage_object_t for a logical disk structure.
     */
    int (*allocate_logical_disk)(char * name,
                                 storage_object_t * * new_disk);

    /*
     * Free a storage_object_t for a logical disk.
     */
    int (*free_logical_disk)(storage_object_t * disk);

    /*
     * Allocate a storage_object_t for a disk_segment.  The caller is
     * responsible for putting the storage_object_t for the logical disk from
     * which this segment comes into the child_objects list in the
     * storage_object_t for the segment.  Additionally, the caller must add the
     * storage_object_t for the disk segment to the parent_objects list in the
     * storage_object_t for the logical disk.
     */
    int (*allocate_segment)(char * name,
                            storage_object_t * * new_segment);

    /*
     * Free a storage_object_t for a disk_segment.
     */
    int (*free_segment)(storage_object_t * segment);

    /*
     * Allocate a storage_container_t structure.  The caller fills in the
     * objects_consumed and objects_produced lists in the container.  The caller
     * fills in the appropriate consuming_container and producing_container
     * fields in the storage_object_t(s) that are consumed or produced by the
     * container.
     */
    int (*allocate_container)(char * name,
                              storage_container_t * * new_container);

    /*
     * Free a storage_container_t structure.
     */
    int (*free_container)(storage_container_t * container);

    /*
     * Allocate a storage_object_t for a storage_region.  The caller is
     * responsible for putting the storage_object_t from which this region comes
     * into the child_objects list in the storage_object_t for the region.
     * Additionally, the caller must add the storage_object_t for the region to
     * the parent_objects list in the storage_object_t from which this region
     * comes.
     */
    int (*allocate_region)(char * name,
                           storage_object_t * * new_region);

    /*
     * Free the storage_region structure.
     */
    int (*free_region)(storage_object_t * region);

    /*
     * Allocate a storage_object_t for an EVMS object.  The caller is
     * responsible for putting the storage_object_t from which this EVMS object
     * comes into the child_objects list in the storage_object_t for the EVMS
     * object.  Additionally, the caller must add the storage_object_t for the
     * EVMS object to the parent_objects list in the storage_object_t from which
     * this EVMS object comes.
     */
    int (*allocate_evms_object)(char * name,
                                storage_object_t * * new_object);

    /*
     * Free a storage_object_t for an EVMS object.
     */
    int (*free_evms_object)(storage_object_t * object);

    /*
     * engine_alloc is the generic memory allocation service provided by the
     * Engine.  For any memory that plug-ins return to the Engine, the plug-in
     * must use the same malloc() that the Engine uses so that the Engine can
     * properly free() the memory.  To assist the plug-ins, the Engine provides
     * a common allocation function which the plug-ins can use so that all
     * memory allocations are managed by the same memory manager.  Memory will
     * be zero filled.
     */
    void * (*engine_alloc)(u_int32_t size);

    /*
     * engine_free is the generic memory deallocation service provided by the
     * Engine.
     */
    void (*engine_free)(void *);

    /*
     * Check if there are any changes pending in the Engine.
     */
    BOOLEAN (*changes_pending)(void);

    /*
     * Tell the Engine that there are changes pending, i.e., there is stuff to
     * be committed to disk.
     */
    void (*set_changes_pending)(void);

    /*
     * Check if the Engine is in the process of committing changes.
     */
    BOOLEAN (*commit_in_progress)(void);

    /*
     * Write data to the Engine's log file.
     */
    int (*write_log_entry)(debug_level_t     level,
                           plugin_record_t * plugin,
                           char            * fmt,
                           ...);

    /*
     * Calculate a 32-bit CRC for a buffer of a given size.
     * On the first call to calculate_CRC() the CRC parameter must be
     * 0xffffffff.
     * calculate_CRC() can be called multiple times to get the CRC for an
     * aggregate of buffers.  To do so, subsequent calls set the CRC parameter
     * to the resulting CRC that was returned from the previous call.
     * To calculate a new CRC, the CRC parameter must be set to 0xffffffff.
     */
    u_int32_t (*calculate_CRC)(u_int32_t crc,
                               void    * buffer,
                               u_int32_t buffer_size);

    /*
     * Calculate a checksum on a buffer of given size.  This Engine service
     * actually issues an ioctl() to the EVMS kernel to use the kernel's
     * checksum function so that checksums are consistent with the runtime
     * code.  An error code is returned if the ioctl to the kernel fails.
     * "insum" is the initial checksum value, useful if you are doing a
     * single checksum on a series of multiple data blocks.
     */
    int (*calculate_checksum)(unsigned char * buffer,
                              int             buffer_size,
                              unsigned int    insum,
                              unsigned int  * outsum);

    /*
     * Add sectors that are to be written with zeros to the Engine's Kill Sector
     * list.  Should only be called by device managers
     */
    int (*add_sectors_to_kill_list)(storage_object_t * disk,     /* Disk on which the sectors reside */
                                    lba_t              lba,      /* Sector number of the first sector */
                                                                 /* to wipe out */
                                    sector_count_t     count);   /* Number of sectors to wipe out */


    /*
     * Tell the Engine that this volume should be rediscovered when the changes
     * are committed.  Call this function if you make changes to the volume's
     * underlying objects, regions, etc.  that will have to be discovered by the
     * kernel runtime code in order to build the volume correctly.
     * Set sync_fs to TRUE if you want the file system on the volume to
     * be synced in a safe state before the volume is rediscovered.
     */
    int (*rediscover_volume)(logical_volume_t * volume,
                             BOOLEAN            sync_fs);

    /*
     * Check to make sure this name is valid and no other object has the same
     * name.
     */
    int (*validate_name)(char * name);

    /*
     * Register the name for an object.  The Engine will make sure that there is
     * no other object with the same name.  If the name is not valid (e.g., it's
     * too long) or another object has already registered the name, an error
     * will be returned.
     */
    int (*register_name)(char * name);

    /*
     * Unregister the name of an object.
     */
    int (*unregister_name)(char * name);

    /*
     * Ask all the parent objects of this object if they can handle this object
     * expanding by the specified amount.  Parent plug-ins may modify the size
     * according to any constrains they have.  If the size has not been changed
     * by any of the parents, the Engine will return 0.  If all the parents
     * don't return an error but the size has been updated, the Engine will
     * return EAGAIN.
     */
    int (*can_expand_by)(storage_object_t * object,
                         sector_count_t   * delta_size);

    /*
     * Ask all the parent objects of this object if they can handle this object
     * shrinking by the specified amount.  Parent plug-ins may modify the size
     * according to any constrains they have.  If the size has not been changed
     * by any of the parents, the Engine will return 0.  If all the parents
     * don't return an error but the size has been updated, the Engine will
     * return EAGAIN.
     */
    int (*can_shrink_by)(storage_object_t * object,
                         sector_count_t   * delta_size);

    /*
     * Send a message to the user interface.  This service can be used in three
     * ways.
     *
     * 1) Send a notification message to the user expecting no response.
     *
     * user_message(plugin_record, NULL, NULL, message_fmt, ...);
     *
     * 2) Ask a question and get one item selected from a list of two or more
     *    items.
     *
     * char * choices = {string1, string2, ..., NULL};
     * user_message(plugin_record, &answer, choices, message_fmt, ...);
     *
     * The "choices" parameter is a NULL terminated array of strings that
     * describe each of the choices.  "*answer" *must* be initialized to the
     * default response.  The UI will present the message and the choices to
     * the user.  On return, *answer will contain the index of the selected
     * choice string.
     */
    int (*user_message)(plugin_record_t * plugin,
                        int             * answer,
                        char          * * choice_text,
                        char            * message_fmt,
                        ...);

    /*
     * user_communication() uses the option_descriptor_t structures to convey a
     * group of choices to the user.  Use this service when you have a complex
     * list of things to ask of the user, e.g., they are of several different
     * types (strings, ints, etc), they have constraints on their selection, or
     * they may have dependencies on each other.
     *
     * The Engine will create a EVMS_Task_Message task for the UI.  The UI will
     * use the task when calling the evms_get_option_descriptor(),
     * evms_set_option_value(), etc.  APIs for getting and setting options.
     * Your plug-in will be called on its set_option() function with the task
     * context.  The action will be EVMS_Task_Message, the task object will be
     * set to the object_instance parameter that you provide on the call to
     * user_communication().
     *
     * The "message_text" will be treated by the UI as a title for the options
     * that are presented.  "options" is an array of option_descriptor_t
     * structures.  Each of the option descriptors *must* have an initial value.
     */
    int (*user_communication)(void                * object_instance,
                              char                * message_text,
                              option_desc_array_t * options);

#if (EVMS_ABI_CODE >= 110)		/* New for version 8 */
    /*
     * Start, update, or close a progress indicator for the user.  See the
     * description in common.h for how the progress_t structures are used.
     */
    int (*progress)(progress_t * progress);
#endif

    /*
     * Can this object be renamed?  The Engine will figure out if there are any
     * restrictions that would prevent the object from being renamed, e.g., the
     * object is the topmost object of a compatibility volume (the volume name
     * will have been derived from the object) and the volume is mounted.  The
     * Engine won't allow a volume that is mounted to be renamed.  If the
     * object cannot be renamed, the Engine will return an error code that
     * (hopefully) gives some indication as to why the rename is not allowed.
     * Plug-ins call this Engine service before allowing their object name to
     * be changed by a set_info() call.
     */
    int (*can_rename)(storage_object_t * object);

    /*
     * Is this volume mounted?  If you want to know the name of the mount point,
     * specify a location in mount_name where the service will place a pointer
     * to malloced memory that contains the mount point name.  Remember to free
     * the string when you are finished with it.  If you do not want to know the
     * mount point and not have the hassle of freeing the memory, specify NULL
     * for mount_name.
     */
    BOOLEAN (*is_mounted)(char   * volume_name,
                          char * * mount_name);

#if (EVMS_ABI_CODE >= 110)		/* New for version 8 */
    /*
     * Assign an FSIM to a volume.  FSIMs can use this service to claim control
     * of a volume.  For example, an FSIM for a journaling file system may want
     * to claim another volume for an external log.
     * The Engine will return an error code if there is any reason the FSIM
     * cannot be assigned to the volume, such as the volume already being owned
     * by another FSIM.
     * An FSIM does not use this service as part of the processing of a call to
     * the FSIM's is_this_yours() function.  The Engine will automatically
     * assign the FSIM to a volume if it returns 0 on a call to is_this_yours().
     *
     */
    int (*assign_fsim_to_volume)(plugin_record_t  * fsim,
                                 logical_volume_t * volume);

    /*
     * Unassign an FSIM from a volume.  FSIMs can use this service to release
     * control of a volume.  For example, on unmkfs_setup() an FSIM for a
     * journaling file system may want to release its claim on another volume
     * that it used for an external log.
     */
    int (*unassign_fsim_from_volume)(logical_volume_t * volume);
#endif

#if (EVMS_ABI_CODE >= 120)
    /*
     * Get the mode in which the Engine was opened.
     */
    engine_mode_t (*get_engine_mode)(void);
#endif

} engine_functions_t;


#if (EVMS_ABI_CODE == 100)
#define ENGINE_PLUGIN_API_MAJOR_VERION  3
#elif (EVMS_ABI_CODE == 110)
#define ENGINE_PLUGIN_API_MAJOR_VERION  8
#elif (EVMS_ABI_CODE == 120)
#define ENGINE_PLUGIN_API_MAJOR_VERION  9
#else
#error Unknown EVMS_ABI
#endif /* EVMS_ABI_CODE */
#define ENGINE_PLUGIN_API_MINOR_VERION  0
#define ENGINE_PLUGIN_API_PATCH_LEVEL   0

typedef struct plugin_functions_s {
#if (EVMS_ABI_CODE >= 120)
    int (*setup_evms_plugin)(engine_functions_t * functions);
#else	
    int (*setup_evms_plugin)(engine_mode_t        mode,
                             engine_functions_t * functions);
#endif

    void (*cleanup_evms_plugin)(void);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Can you apply your plug-in to the input_object?  If yes, return the size
     * of the object you would create.
     * The Engine will only call this function on EVMS feature plug-ins.
     * Other plug-ins may choose whether or not to support this API.
     */
    int (*can_add_feature)(storage_object_t * input_object,
                           sector_count_t   * size);
#endif

    /*
     * Can you delete this object?
     */
    int (*can_delete)(storage_object_t * object);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Can you unassign your plug-in from this object?
     */
    int (*can_unassign)(storage_object_t * object);
#endif

    /*
     * Can you expand this object?  If yes, build an expand_object_info_t and
     * add it to the expand_points list.  If you can't expand, but allow one of
     * your children to expand, call can_expand on whichever child you will
     * allow to expand.  If you can not handle expanding below you, do not pass
     * the command down to your child.
     */
    int (*can_expand)(storage_object_t * object,
                      sector_count_t   * expand_limit,      // a delta size
                      dlist_t            expand_points);    // of type expand_object_info_t,
                                                            // tag = EXPAND_OBJECT_TAG

    /*
     * Can you allow your child object to expand by "size"?  Return 0 if yes,
     * else an error code.  "size" is the delta expand BY size, not the
     * resulting size.  Update the "size" if your object would expand by a
     * different delta size when your child object expanded by the given size.
     */
    int (*can_expand_by)(storage_object_t * object,
                         sector_count_t   * size);

    /*
     * Can you shrink this object?  If yes, build a shrink_object_info_t and
     * add it to the shrink_points list.  If you can't shrink, but allow one of
     * your children to shrink, call can_shrink on whichever child you will
     * allow to shrink.  If you can not handle shrinking below you, do not pass
     * the command down to your child.
     */
    int (*can_shrink)(storage_object_t * object,
                      sector_count_t   * shrink_limit,      // a delta size
                      dlist_t            shrink_points);    // of type shrink_object_info_t,
                                                            // tag = SHRINK_OBJECT_TAG


    /*
     * Can you allow your child object to shrink by "size"?  Return 0 if yes,
     * else an error code.  "size" is the delta shrink BY size, not the
     * resulting size.  Update the "size" if your object would shrink by a
     * different delta size when your child object shrunk by the given size.
     */
    int (*can_shrink_by)(storage_object_t * object,
                         sector_count_t   * size);

#if (EVMS_ABI_CODE >= 120)
    /*
     * Can you replace this object's child with another object?
     */
    int (*can_replace_child)(storage_object_t * object,
                             storage_object_t * child);
#else
    /*
     * Can you move this object?
     */
    int (*can_move)(storage_object_t * object);
#endif

    /*
     * Will you allow your object to be made into a volume?  (We don't see
     * any reason why you wouldn't.)  Will you allow a volume to be reverted
     * off the top of your object?  The "flag" parameter says whether the
     * volume is to be created (TRUE) or removed (FALSE).
     */
    int (*can_set_volume)(storage_object_t * object,
                          BOOLEAN            flag);

    /*
     * Claim objects by removing them from the list.  Create a storage_object_t
     * for the object you are discovering, fill in the appropriate fields and
     * put the new object on the output_objects list.  If you do not claim an
     * object from the input list, then just copy/move it to the output list.
     * The input list can be modified at will.  The output list must contain
     * all the storage objects in the system after yours are discovered, i.e.,
     * it is the input list, minus the objects you claim, plus the objects you
     * produce.
     */
    int (*discover)(dlist_t input_objects,
                    dlist_t output_objects,
                    BOOLEAN final_call);

    /*
     * Create storage_object_t(s) from the list of objects using the given
     * options.  Return the newly allocated storage_object_t(s) in new_objects
     * list.
     */
    int (*create)(dlist_t          input_objects,
                  option_array_t * options,
                  dlist_t          output_objects);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Assign your plug-in to produce storage objects from the given storage
     * object.  This function makes sense mainly for segment managers that are
     * assigned to disks (or segments).
     */
    int (*assign)(storage_object_t * object,
                  option_array_t   * options);
#endif

    /*
     * Delete the object.  Free any privately allocated data.  Remove your
     * parent pointer from your child objects.  Do any cleanup necessary to
     * remove your plug-in from your child objects.  Put your object's children
     * from the object's child_objects dlist_t onto the dlist_t provided in the
     * second parameter.  Call the Engine's free_?????t() to free the object.
     */
    int (*delete)(storage_object_t * object,
                  dlist_t            child_objects);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Unassign your plug-in from producing storage objects from the given
     * storage object.  This function makes sense mainly for segment managers
     * that are assigned to disks (or segments).
     */
    int (*unassign)(storage_object_t * object);
#endif

    /*
     * If the "object" is not the "expand_object", then your child is going to
     * expand.  Do any necessary work to get ready for your child to expand,
     * e.g., read in meta data, then call expand() on your child object which
     * will expand.  Upon return from the call to your child's expand(), do
     * any work necessary to adjust this object to account for the child
     * object's new size, e.g., update the location of meta data.
     * If the "object" is the same as the "expand_object", then this is the
     * object targeted for expanding.  Expand the object according to the
     * input_objects given and the options selected.
     */
    int (*expand)(storage_object_t * object,
                  storage_object_t * expand_object,
                  dlist_t            input_objects,
                  option_array_t   * options);

    /*
     * If the "object" is not the "shrink_object", then your child is going to
     * shrink.  Do any necessary work to get ready for your child to shrink,
     * e.g., read in meta data, then call shrink() on your child object which
     * will shrink.  Upon return from the call to your child's shrink(), do
     * any work necessary to adjust this object to account for the child
     * object's new size, e.g., update the location of meta data.
     * If the "object" is the same as the "shrink_object", then this is the
     * object targeted for shrinking.  Shrink the object according to the
     * input_objects given and the options selected.
     */
    int (*shrink)(storage_object_t * object,
                  storage_object_t * shrink_object,
                  dlist_t            input_objects,
                  option_array_t   * options);

#if (EVMS_ABI_CODE >= 120)
    /*
     * Replace the object's child with the new child object.
     */
    int (*replace_child)(storage_object_t * object,
                         storage_object_t * child,
                         storage_object_t * new_child);
#else
    /*
     * Move the contents of the source object to the target object using the
     * given options.
     */
    int (*move)(storage_object_t * source,
                storage_object_t * target,
                option_array_t   * options);
#endif

    /*
     * This call notifies you that your object is being made into (or part of)
     * a volume or that your object is no longer part of a volume.  The "flag"
     * parameter indicates whether the volume is being created (TRUE) or
     * removed (FALSE).
     */
    void (*set_volume)(storage_object_t * object,
                       BOOLEAN            flag);

    /*
     * Put sectors on the kill list.  The plug-in translates the lsn and count
     * into lsn(s) and count(s) for its child object(s) and calls the child
     * object's add_sectors_to_kill_list().
     * The Device Manager calls the Engine's add_sectors_to_kill_list service
     * to put the sectors on the Engine's kill list.
     */
    int (*add_sectors_to_kill_list)(storage_object_t * object,
                                    lsn_t              lsn,
                                    sector_count_t     count);

    /*
     * Write your plug-ins data, e.g., feature header and feature meta data, to
     * disk.  Clear the SOFLAG_DIRTY in the storage_object_t(s).
     * Committing changes in done in several (two for now) phases.  "phase"
     * says which phase of the commit is being performed.
     * Write your first copy of meta data during phase 1; write your second
     * copy of meta data (if you have one) during phase 2.
     */
    int (*commit_changes)(storage_object_t * object,
                          uint               phase);

    /*
     * Return the total number of supported options for the specified task.
     */
    int (*get_option_count)(task_context_t * context);

    /*
     * Fill in the initial list of acceptable objects.  Fill in the minimum and
     * maximum number of objects that must/can be selected.  Set up all initial
     * values in the option_descriptors in the context record for the given
     * task.  Some fields in the option_descriptor may be dependent on a
     * selected object.  Leave such fields blank for now, and fill in during the
     * set_objects call.
     */
    int (*init_task)(task_context_t * context);

    /*
     * Examine the specified value, and determine if it is valid for the task
     * and option_descriptor index. If it is acceptable, set that value in the
     * appropriate entry in the option_descriptor. The value may be adjusted
     * if necessary/allowed. If so, set the effect return value accordingly.
     */
    int (*set_option)(task_context_t * context,
                      u_int32_t        index,
                      value_t        * value,
                      task_effect_t  * effect);

    /*
     * Validate the objects in the selected_objects dlist in the task context.
     * Remove from the selected objects lists any objects which are not
     * acceptable.  For unacceptable objects, create a declined_handle_t
     * structure with the reason why it is not acceptable, and add it to the
     * declined_objects dlist.  Modify the acceptable_objects dlist in the task
     * context as necessary based on the selected objects and the current
     * settings of the options.  Modify any option settings as necessary based
     * on the selected objects.  Return the appropriate task_effect_t settings
     * if the object list(s), minimum or maximum objects selected, or option
     * settings have changed.
     */
    int (*set_objects)(task_context_t * context,
                       dlist_t          declined_objects,    /* of type declined_handle_t */
                       task_effect_t  * effect);

    /*
     * Return any additional information that you wish to provide about the
     * object.  The Engine provides an external API to get the information
     * stored in the storage_object_t.  This call is to get any other
     * information about the object that is not specified in the
     * storage_object_t.  Any piece of information you wish to provide must be
     * in an extended_info_t structure.  Use the Engine's engine_alloc() to
     * allocate the memory for the extended_info_t.  Also use engine_alloc() to
     * allocate any strings that may go into the extended_info_t.  Then use
     * engine_alloc() to allocate an extended_info_array_t with enough entries
     * for the number of extended_info_t structures you are returning.  Fill
     * in the array and return it in *info.
     * If you have extended_info_t descriptors that themselves may have more
     * extended information, set the EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag
     * in the extended_info_t flags field.  If the caller wants more information
     * about a particular extended_info_t item, this API will be called with a
     * pointer to the storage_object_t and with a pointer to the name of the
     * extended_info_t item.  In that case, return an extended_info_array_t with
     * further information about the item.  Each of those items may have the
     * EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag set if you desire.  It is your
     * responsibility to give the items unique names so that you know which item
     * the caller is asking additional information for.  If info_name is NULL,
     * the caller just wants top level information about the object.
     */
    int (*get_info)(storage_object_t        * object,
                    char                    * info_name,
                    extended_info_array_t * * info);

    /*
     * Apply the settings of the options to the given object.
     */
    int (*set_info)(storage_object_t * object,
                    option_array_t   * options);

    /*
     * Return any additional information that you wish to provide about your
     * plug-in.  The Engine provides an external API to get the information
     * stored in the plugin_record_t.  This call is to get any other
     * information about the plug-in that is not specified in the
     * plugin_record_t.  Any piece of information you wish to provide must be
     * in an extended_info_t structure.  Use the Engine's engine_alloc() to
     * allocate the memory for the extended_info_t.  Also use engine_alloc() to
     * allocate any strings that may go into the extended_info_t.  Then use
     * engine_alloc() to allocate an extended_info_array_t with enough entries
     * for the number of extended_info_t structures you are returning.  Fill
     * in the array and return it in *info.
     * If you have extended_info_t descriptors that themselves may have more
     * extended information, set the EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag
     * in the extended_info_t flags field.  If the caller wants more information
     * about a particular extended_info_t item, this API will be called with a
     * pointer to the storage_object_t and with a pointer to the name of the
     * extended_info_t item.  In that case, return an extended_info_array_t with
     * further information about the item.  Each of those items may have the
     * EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag set if you desire.  It is your
     * responsibility to give the items unique names so that you know which item
     * the caller is asking additional information for.  If info_name is NULL,
     * the caller just wants top level information about the object.
     */
    int (*get_plugin_info)(char                    * info_name,
                           extended_info_array_t * * info);

    /*
     * Convert lsn and count to lsn and count on the child object(s) and and
     * call the read function of child objects.
     */
    int (*read)(storage_object_t * object,
                lsn_t              lsn,
                sector_count_t     count,
                void             * buffer);

    /*
     * Convert lsn and count to lsn and count on the child object(s) and and
     * call the write function of child objects.
     */
    int (*write)(storage_object_t * object,
                 lsn_t              lsn,
                 sector_count_t     count,
                 void             * buffer);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Return an array of plug-in functions that you support for this object.
     */
    int (*get_plugin_functions)(storage_object_t        * object,
                                function_info_array_t * * actions);

    /*
     * Execute the plug-in function on the object.
     */
    int (*plugin_function)(storage_object_t * object,
                           task_action_t      action,
                           dlist_t            objects,
                           option_array_t   * options);
#endif

    /*
     * Generic method for communicating with your plug-in.
     */
    int (*direct_plugin_communication)(void    * thing,
                                       BOOLEAN   target_kernel_plugin,
                                       void    * arg);

} plugin_functions_t;


#if (EVMS_ABI_CODE >= 110)
#define ENGINE_FSIM_API_MAJOR_VERION  8
#define ENGINE_FSIM_API_MINOR_VERION  0
#define ENGINE_FSIM_API_PATCH_LEVEL   0
#endif

typedef struct fsim_functions_s {
#if (EVMS_ABI_CODE >= 120)
    int (*setup_evms_plugin)(engine_functions_t * functions);
#else	
    int (*setup_evms_plugin)(engine_mode_t        mode,
                             engine_functions_t * functions);
#endif

    void (*cleanup_evms_plugin)(void);

    /*
     * Does this FSIM manage the file system on this volume?
     * Return 0 for "yes", else a reason code.
     */
    int (*is_this_yours)(logical_volume_t * volume);

    /*
     * Get the current size of the file system on this volume.
     */
    int (*get_fs_size)(logical_volume_t * volume,
                       sector_count_t   * fs_size);

    /*
     * Get the file system size limits for this volume.
     */
    int (*get_fs_limits)(logical_volume_t * volume,
                         sector_count_t   * fs_min_size,
                         sector_count_t   * fs_max_size,
                         sector_count_t   * vol_max_size);

    /*
     * Can you install your file system on this volume?
     */
    int (*can_mkfs)(logical_volume_t * volume);

    /*
     * Can you remove your file system from this volume?
     */
    int (*can_unmkfs)(logical_volume_t * volume);

    /*
     * Can you fsck this volume?
     */
    int (*can_fsck)(logical_volume_t * volume);

    /*
     * Can you defrag this volume?
     */
    int (*can_defrag)(logical_volume_t * volume);

    /*
     * Can you expand this volume by the amount specified?
     * If your file system cannot handle expansion at all, return an
     * error code that indicates why it cannot be expanded..
     * If your file system can expand but cannot handle having unused
     * space after the end of your file system, adjust the *delta_size
     * to the maximum you allow and return 0.
     * If your file system cannot fill the resulting size but your file
     * system can handle extra unused space after the end of the file
     * system, then do not change the *delta_size and return 0.
     */
    int (*can_expand_by)(logical_volume_t * volume,
                         sector_count_t   * delta_size);

    /*
     * Can you shrink this volume by the amount specified?
     * If your file system cannot handle shrinking at all, return an
     * error code that indicates why it cannot be shrunk.
     * If your file system can shrink but the *delta_size is too much to
     * shrink by, adjust the *delta_size to the maximum shrinkage you allow and
     * return 0.
     */
    int (*can_shrink_by)(logical_volume_t * volume,
                         sector_count_t   * delta_size);

#if (EVMS_ABI_CODE >= 110)		/* New for version 8 */
    /*
     * mkfs has been scheduled.  Do any setup work such as claiming another
     * volume for an external log.
     */
    int (*mkfs_setup)(logical_volume_t * volume,
                      option_array_t   * options);
#endif

    /*
     * Install your file system on the volume.
     */
    int (*mkfs)(logical_volume_t * volume,
                option_array_t   * options);

#if (EVMS_ABI_CODE >= 110)		/* New for version 8 */
    /*
     * unmkfs has been scheduled.  Do any setup work such as releasing another
     * volume that was used for an external log.
     */
    int (*unmkfs_setup)(logical_volume_t * volume);
#endif

    /*
     * Remove your file system from the volume.  This could be as simple as
     * wiping out critical sectors, such as a superblock, so that you will
     * no longer detect that your file system is installed on the volume.
     */
    int (*unmkfs)(logical_volume_t * volume);

    /*
     * Run fsck on the volume.
     */
    int (*fsck)(logical_volume_t * volume,
                option_array_t   * options);

    /*
     * Defragment on the volume.
     */
    int (*defrag)(logical_volume_t * volume,
                  option_array_t   * options);

    /*
     * Expand the volume to new_size.  If the volume is not expanded exactly to
     * new_size, set new_sie to the new_size of the volume.
     */
    int (*expand)(logical_volume_t * volume,
                  sector_count_t   * new_size);

    /*
     * Shrink the volume to new_size.  If the volume is not expanded exactly to
     * new_size, set new_size to the new_size of the volume.
     */
    int (*shrink)(logical_volume_t * volume,
                  sector_count_t     requested_size,
                  sector_count_t   * new_size);

    /*
     * Return the total number of supported options for the specified task.
     */
    int (*get_option_count)(task_context_t * context);

    /*
     * Fill in the initial list of acceptable objects.  Fill in the minimum and
     * maximum number of objects that must/can be selected.  Set up all initial
     * values in the option_descriptors in the context record for the given
     * task.  Some fields in the option_descriptor may be dependent on a
     * selected object.  Leave such fields blank for now, and fill in during the
     * set_objects call.
     */
    int (*init_task)(task_context_t * context);

    /*
     * Examine the specified value, and determine if it is valid for the task
     * and option_descriptor index. If it is acceptable, set that value in the
     * appropriate entry in the option_descriptor. The value may be adjusted
     * if necessary/allowed. If so, set the effect return value accordingly.
     */
    int (*set_option)(task_context_t * context,
                      u_int32_t        index,
                      value_t        * value,
                      task_effect_t  * effect);

    /*
     * Validate the volumes in the selected_objects dlist in the task context.
     * Remove from the selected objects lists any volumes which are not
     * acceptable.  For unacceptable volumes, create a declined_handle_t
     * structure with the reason why it is not acceptable, and add it to the
     * declined_volumes dlist.  Modify the acceptable_objects dlist in the task
     * context as necessary based on the selected objects and the current
     * settings of the options.  Modify any option settings as necessary based
     * on the selected objects.  Return the appropriate task_effect_t settings
     * if the object list(s), minimum or maximum objects selected, or option
     * settings have changed.
     */
    int (*set_volumes)(task_context_t * context,
                       dlist_t          declined_volumes,    /* of type declined_handle_t */
                       task_effect_t  * effect);


    /*
     * Return any additional information that you wish to provide about the
     * volume.  The Engine provides an external API to get the information
     * stored in the logical_volume_t.  This call is to get any other
     * information about the volume that is not specified in the
     * logical_volume_t.  Any piece of information you wish to provide must be
     * in an extended_info_t structure.  Use the Engine's engine_alloc() to
     * allocate the memory for the extended_info_t.  Also use engine_alloc() to
     * allocate any strings that may go into the extended_info_t.  Then use
     * engine_alloc() to allocate an extended_info_array_t with enough entries
     * for the number of extended_info_t structures you are returning.  Fill
     * in the array and return it in *info.
     * If you have extended_info_t descriptors that themselves may have more
     * extended information, set the EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag
     * in the extended_info_t flags field.  If the caller wants more information
     * about a particular extended_info_t item, this API will be called with a
     * pointer to the storage_object_t and with a pointer to the name of the
     * extended_info_t item.  In that case, return an extended_info_array_t with
     * further information about the item.  Each of those items may have the
     * EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag set if you desire.  It is your
     * responsibility to give the items unique names so that you know which item
     * the caller is asking additional information for.  If info_name is NULL,
     * the caller just wants top level information about the object.
     */
    int (*get_volume_info)(logical_volume_t        * volume,
                           char                    * info_name,
                           extended_info_array_t * * info);

    /*
     * Apply the settings of the options to the given volume.
     */
    int (*set_volume_info)(logical_volume_t * volume,
                           option_array_t   * options);

    /*
     * Return any additional information that you wish to provide about your
     * plug-in.  The Engine provides an external API to get the information
     * stored in the plugin_record_t.  This call is to get any other
     * information about the plug-in that is not specified in the
     * plugin_record_t.  Any piece of information you wish to provide must be
     * in an extended_info_t structure.  Use the Engine's engine_alloc() to
     * allocate the memory for the extended_info_t.  Also use engine_alloc() to
     * allocate any strings that may go into the extended_info_t.  Then use
     * engine_alloc() to allocate an extended_info_array_t with enough entries
     * for the number of extended_info_t structures you are returning.  Fill
     * in the array and return it in *info.
     * If you have extended_info_t descriptors that themselves may have more
     * extended information, set the EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag
     * in the extended_info_t flags field.  If the caller wants more information
     * about a particular extended_info_t item, this API will be called with a
     * pointer to the storage_object_t and with a pointer to the name of the
     * extended_info_t item.  In that case, return an extended_info_array_t with
     * further information about the item.  Each of those items may have the
     * EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag set if you desire.  It is your
     * responsibility to give the items unique names so that you know which item
     * the caller is asking additional information for.  If info_name is NULL,
     * the caller just wants top level information about the object.
     */
    int (*get_plugin_info)(char                    * info_name,
                           extended_info_array_t * * info);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Return an array of plug-in functions that you support for this volume.
     */
    int (*get_plugin_functions)(logical_volume_t        * volume,
                                function_info_array_t * * actions);

    /*
     * Execute the plug-in function on the volume.
     */
    int (*plugin_function)(logical_volume_t * volume,
                           task_action_t      action,
                           dlist_t            objects,
                           option_array_t   * options);
#endif

    /*
     * Generic method for communicating with your plug-in.
     */
    int (*direct_plugin_communication)(void  * thing,
                                       BOOLEAN target_kernel_plugin,
                                       void  * arg);

} fsim_functions_t;


#if (EVMS_ABI_CODE >= 110)
#define ENGINE_CONTAINER_API_MAJOR_VERION  8
#define ENGINE_CONTAINER_API_MINOR_VERION  0
#define ENGINE_CONTAINER_API_PATCH_LEVEL   0
#endif

typedef struct container_functions_s {

    /*
     * Can you create a container from this list of data segments?
     */
    int (*can_create_container)(dlist_t objects);

    /*
     * Can you destroy the container?  You must check to be sure that no regions
     * are exported from this container.
     */
    int (*can_delete_container)(storage_container_t * container);

    /*
     * Can you add this object to the container?
     * Return 0 if you can, else return an error code.
     */
    int (*can_add_object)(storage_object_t    * object,
                          storage_container_t * container);

    /*
     * Can you remove this object from the container that currently consumes
     * it?  Return 0 if you can, else return an error code.
     */
    int (*can_remove_object)(storage_object_t * object);

    /*
     * Create and fill in the container adding newly created unallocated objects
     * produced as appropriate.  The plug-in must claim the objects, as it does
     * in discovery.  Mark the container dirty.  Must use allocate_container
     * engine API to allocate the container structure.
     */
    int (*create_container)(dlist_t                 objects,
                            option_array_t        * options,
                            storage_container_t * * container);

    /*
     * Engine will remove the object from its current container before calling
     * this API.  Claim the object and add it to a container objects_consumed
     * list.  Mark the container dirty.  Update/allocate the unallocated object
     * that is exported from the container.  If container is NULL, add the
     * object to default (or unassigned) container.
     */
    int (*add_object)(storage_object_t    * object,
                      storage_container_t * container,
                      option_array_t      * options);

    /*
     * Transfer the object from its current container to the specified
     * container.  Mark the container dirty.  If container is NULL, transfer
     * the object to the default (or unassigned) container.
     */
    int (*transfer_object)(storage_object_t    * object,
                           storage_container_t * container,
                           option_array_t      * options);

    /*
     * Remove object from its current container.  Make sure there are no
     * allocated objects produced by the container that are using space in the
     * object.  Does not destroy segment.
     */
    int (*remove_object)(storage_object_t * object);

    /*
     * Destroy the container.  Make sure there are no allocated objects being
     * produced by the container.  Put your consumed objects from the
     * container's objects_consumed dlist_t onto the dlist_t provided in the
     * second parameter.  Free any private data, then use the Engine's
     * free_container() to deallocate the container object.
     */
    int (*delete_container)(storage_container_t * container,
                            dlist_t               objects_consumed);

    /*
     * Write any container meta data, to disk.  Clear the SCFLAG_DIRTY in the
     * container.
     * Committing changes in done in several (two for now) phases.  "phase"
     * says which phase of the commit is being performed.
     * Write your first copy of meta data during phase 1; write your second
     * copy of meta data (if you have one) during phase 2.
     */
    int (*commit_container_changes)(storage_container_t * container,
                                    uint                  phase);

    /*
     * Return any additional information that you wish to provide about the
     * container.  The Engine provides an external API to get the information
     * stored in the storage_container_t.  This call is to get any other
     * information about the container that is not specified in the
     * storage_container_t.  Any piece of information you wish to provide must
     * be in an extended_info_t structure.  Use the Engine's engine_alloc() to
     * allocate the memory for the extended_info_t.  Also use engine_alloc() to
     * allocate any strings that may go into the extended_info_t.  Then use
     * engine_alloc() to allocate an extended_info_array_t with enough entries
     * for the number of extended_info_t structures you are returning.  Fill
     * in the array and return it in *info.
     * If you have extended_info_t descriptors that themselves may have more
     * extended information, set the EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag
     * in the extended_info_t flags field.  If the caller wants more information
     * about a particular extended_info_t item, this API will be called with a
     * pointer to the storage_container_t and with a pointer to the name of the
     * extended_info_t item.  In that case, return an extended_info_array_t with
     * further information about the item.  Each of those items may have the
     * EVMS_EINFO_FLAGS_MORE_INFO_AVAILABLE flag set if you desire.  It is your
     * responsibility to give the items unique names so that you know which item
     * the caller is asking additional information for.  If info_name is NULL,
     * the caller just wants top level information about the object.
     */
    int (*get_container_info)(storage_container_t     * container,
                              char                    * info_name,
                              extended_info_array_t * * info);

    /*
     * Apply the settings of the options to the given container.
     */
    int (*set_container_info)(storage_container_t * container,
                              option_array_t      * options);

#if (EVMS_ABI_CODE >= 110)
    /*
     * Return an array of plug-in functions that you support for this container.
     */
    int (*get_plugin_functions)(storage_container_t     * container,
                                function_info_array_t * * actions);

    /*
     * Execute the plug-in function on the container.
     */
    int (*plugin_function)(storage_container_t * container,
                           task_action_t         action,
                           dlist_t               objects,
                           option_array_t      * options);
#endif

} container_functions_t;

#endif

