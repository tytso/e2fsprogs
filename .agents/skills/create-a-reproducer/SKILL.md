---
name: create-a-reproducer
description: Create a minimal, self-contained POSIX shell script reproducer for an e2fsprogs bug or code path using a small ext4 filesystem image synthesized with mke2fs and debugfs (without root privileges or loop mounts). Use when asked to reproduce a bug, craft a corrupted filesystem image, or hit a specific code path in e2fsck, libext2fs, resize2fs, debugfs, tune2fs, or mke2fs.
---

# Creating an `e2fsprogs` Reproducer

When creating a standalone reproducer script for a bug or code path in `e2fsprogs`, read and follow the complete guide in [`doc/developers/create-a-reproducer.md`](../../../doc/developers/create-a-reproducer.md).

## Required Reading

Before writing the reproducer script, read:

1. `doc/developers/create-a-reproducer.md` — Complete instructions for tracing preconditions, synthesizing minimal ext4 images with `mke2fs` and `debugfs`, manipulating extent trees/inodes/superblocks/bitmaps/journals/raw blocks, and structuring the reproducer script.
2. `doc/developers/create-a-regression-test.md` — Read this as well if asked to convert the reproducer into a formal `tests/<prefix>_<name>/` regression test.

## Workflow Summary

1. **Trace the target code path in `e2fsprogs`** (see Step 1 of `doc/developers/create-a-reproducer.md`):
   - Identify the exact on-disk preconditions required to reach the target code path in `e2fsck/`, `lib/ext2fs/`, `debugfs/`, `misc/`, or `resize/`.
   - Use binaries built in the current tree (`./e2fsck/e2fsck`, `./misc/mke2fs`, `./debugfs/debugfs`, etc.) while allowing environment variable overrides (`MKE2FS`, `DEBUGFS`, `E2FSCK`, `DUMPE2FS`, `TUNE2FS`, `RESIZE2FS`).

2. **Synthesize a minimal ext4 image from scratch** (see Step 2 of `doc/developers/create-a-reproducer.md`):
   - Do not use `root`, `sudo`, `mount -o loop`, or large binary images.
   - Format a small image (`64 KB`–`1 MB`, e.g., `mke2fs -q -t ext4 -b 1024 -F "$IMG" 100`) with only the necessary `-O` features.
   - Use `debugfs -w "$IMG" << 'EOF'` (`write`, `mkdir`, `setb`/`freeb`, `seti`/`freei`, `bmap`, `extent_open`/`split_node`/`insert_node`/`replace_node`, `sif`, `ssv`, `set_bg`, `ea_set`, `journal_open`, `zap_block`) to construct the exact on-disk state.

3. **Verify and present the reproducer** (see Step 3 of `doc/developers/create-a-reproducer.md`):
   - Write a clean POSIX `/bin/sh` script using `mktemp` and `trap ... EXIT` cleanup.
   - Show the relevant before-state, run the target `e2fsprogs` tool, print its output and exit code, and show the after-state.
   - Execute the script to verify that it reliably triggers the target behavior, and include both the script and its output in the response.
