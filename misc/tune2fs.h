/*
 * tune2fs.h - Change the file system parameters on an ext2 file system
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/* Takes exactly the same args as the tune2fs exectuable.
 * Is the entrypoint for libtune2fs.
 */
int tune2fs_main(int argc, char **argv);
