You are working in the `e2fsprogs` source tree (located in the current working directory `.`).

### Goal
Analyze the described bug or code path in `e2fsprogs` and create a **minimal, self-contained shell script reproducer** that triggers it on a **small ext4 filesystem image** (typically 64 KB – 1 MB) constructed from scratch using `mke2fs` and `debugfs`, without requiring root privileges, loop mounts, or large pre-built binary images.

### Step 1: Trace the Target Code Path in `e2fsprogs`
1. Locate the relevant code path in the source tree (`e2fsck/`, `lib/ext2fs/`, `debugfs/`, `misc/`, `resize/`, etc.).
2. Determine the exact preconditions required on disk to reach the target code path and trigger the bug (e.g., superblock features/fields, inode fields/flags, extent tree depth and layout, bitmap state, directory/xattr/journal structures).
3. Locate the `e2fsprogs` binaries to test against:
   - Prefer binaries built from the current tree if present (e.g., `./e2fsck/e2fsck`, `./misc/mke2fs`, `./debugfs/debugfs`, `./misc/dumpe2fs`, `./misc/tune2fs`, `./resize/resize2fs`), or build them if needed.
   - Allow overriding binary paths via environment variables (`MKE2FS`, `DEBUGFS`, `E2FSCK`, `DUMPE2FS`, `TUNE2FS`, `RESIZE2FS`).

### Step 2: Synthesize a Minimal ext4 Image with `mke2fs` and `debugfs`
Keep the filesystem image as small and fast as possible:
- **Use a small block size and block count**: e.g., `mke2fs -q -t ext4 -b 1024 -F "$IMG" 100` (a 100 KB image with 1024-byte blocks).
- **Toggle features with `-O`** to match preconditions and avoid unnecessary overhead (e.g., `-O ^has_journal`, `-O ^orphan_file`, `-O metadata_csum`, `-O bigalloc`, `-O inline_data`, `-O ea_inode`).
- **Use `debugfs -w "$IMG" << 'EOF'` to construct the exact on-disk state**:
  - **Files & directories**: `write /dev/null /f`, `mkdir`, `mknod`, `symlink`, `link`, `unlink`, `kill_file`, `expand_dir`.
  - **Block & inode bitmaps** (`debugfs/debugfs.c`):
    - `setb <blk> [count]`, `freeb <blk> [count]`, `testb <blk>`, `ffb [count]`
    - `seti <ino> [count]`, `freei <ino> [count]`, `testi <ino>`, `ffi [count]`
  - **Logical-to-physical block mapping**:
    - `bmap <filespec> <lblk> <pblk>`
    - `fallocate <filespec> <start_blk> <end_blk>`
    - `punch <filespec> <start_blk> <end_blk>`
  - **Direct extent-tree manipulation** (`debugfs/extent_cmds.ct`, `debugfs/extent_inode.c`):
    - Do **not** allocate hundreds of fragmented blocks just to force an extent tree split. Instead, open the inode's extent tree with `extent_open <filespec>` (`eo`) and use `split_node` (`split`) to create multi-level (`depth >= 1`) extent trees with only 2–3 extents:
      - Navigation: `root_node` (`root`), `down_node` (`down`), `up_node` (`up`), `next_sib` (`ns`), `prev_sib` (`ps`), `next_leaf` (`nl`), `prev_leaf` (`pl`), `next` (`n`), `prev` (`p`), `goto_block <lblk>`
      - Modification: `insert_node [--after] [--uninit] <lblk> <len> <pblk>`, `replace_node [--uninit] <lblk> <len> <pblk>`, `delete_node`, `split_node` (`split`), `set_bmap [--uninit] <lblk> <pblk>`, `fix_parents` (`fixp`)
      - Inspection & closing: `current_node`, `info`, `print_all` (`all`), `extent_close` (`ec`)
  - **Inode fields** (`debugfs/set_fields.c` — `set_inode_field` / `sif`):
    - `sif <filespec> <field> <value>` (e.g., `size`, `blocks`, `links_count`, `mode`, `flags`, `dtime`, `bmap[0]`, `block[IND]`, `file_acl`, `extra_isize`, `projid`, `checksum`)
  - **Superblock fields** (`debugfs/set_fields.c` — `set_super_value` / `ssv`):
    - `ssv <field> <value>` (e.g., `last_orphan`, `free_blocks_count`, `free_inodes_count`, `state`, `feature_compat`, `feature_incompat`, `feature_ro_compat`, `first_meta_bg`, `desc_size`, `orphan_file_inum`, `checksum_seed`)
  - **Block group descriptors** (`debugfs/set_fields.c` — `set_block_group` / `set_bg`):
    - `set_bg <bg_num> <field> <value>` (e.g., `free_blocks_count`, `free_inodes_count`, `flags`, `itable_unused`, `checksum calc`)
  - **Extended attributes, journals, and raw block corruption**:
    - Xattrs: `ea_set`, `ea_get`, `ea_list`, `ea_rm`
    - Journal (`debugfs/journal.c`): `journal_open` (`jo`), `journal_write` (`jw`), `journal_close` (`jc`), `journal_run` (`jr`), `logdump`
    - Raw block edits (`debugfs/zap.c`): `zap_block [-f <filespec>] [-o <offset>] [-l <length>] [-p <pattern>] <blk>` or `zap_block [-f <filespec>] [-b <bit>] <blk>`, `block_dump` (`bd`)

### Step 3: Verify and Present the Reproducer
1. Write a clean POSIX `/bin/sh` script that:
   - Creates a temporary image via `mktemp` and cleans it up via `trap ... EXIT`.
   - Formats the image with `mke2fs` and applies the `debugfs` commands.
   - Displays the relevant before-state (e.g., `debugfs -R "stat /f"`, `debugfs -R "ex /f"`).
   - Runs the target `e2fsprogs` command (e.g., `e2fsck -p`, `e2fsck -fy`, `e2fsck -fn`, `resize2fs`) and prints its output and exit code (`$?`).
   - Displays the after-state (and optionally a follow-up `e2fsck -fn` check) to clearly demonstrate the failure or corruption.
2. Run the script to verify that it reliably reproduces the issue, and include both the script and its exact output in your response.
3. *(Optional, if requested)* Convert the reproducer into an `e2fsprogs` regression test under `tests/<prefix>_<name>/` (`script`, `expect.1`, `expect.2`, `name`) using `SKIP_GUNZIP="true"` (see `tests/f_extent_oobounds/script` or `tests/f_bigalloc_orphan_list/script` for examples) and verify it with `(cd tests && ./test_one <prefix>_<name>)`.
