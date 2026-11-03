# AGENTS.md — Developer & AI Agent Guide for `e2fsprogs`

`e2fsprogs` is the suite of userspace utilities and libraries for creating, checking, modifying, debugging, and resizing ext2, ext3, and ext4 filesystems.

---

## Repository Layout

- **`lib/ext2fs/`**: `libext2fs` — Core library for reading, writing, and manipulating ext2/ext3/ext4 filesystems (superblocks, block group descriptors, bitmaps, inodes, directories, extent trees, inline data, extended attributes, journals, MMP, I/O channels).
- **`lib/e2p/`**: `libe2p` — Library for parsing and formatting ext2/3/4 superblock fields, filesystem features, mount options, and inode flags.
- **`lib/et/`**: `libcom_err` and `compile_et` — Error-table library and compiler that generates `.c`/`.h` files from `.et.in` error tables (such as `lib/ext2fs/ext2_err.et.in`).
- **`lib/ss/`**: `libss` and `mk_cmds` — Interactive command-line subsystem library and command-table compiler (`.ct` -> `.c`, used by `debugfs`).
- **`lib/support/`**: `libsupport` — Internal support library shared by `e2fsprogs` tools (quota management, `profile` configuration parser, dictionary/rbtree, device name resolution, etc.).
- **`lib/uuid/` & `lib/blkid/`**: UUID generation/parsing (`libuuid`) and block device identification (`libblkid`) libraries.
- **`e2fsck/`**: Filesystem consistency checker (`pass1.c`–`pass5.c`, `journal.c`, `recovery.c`, `super.c`, `extents.c`, `rehash.c`, `problem.c`, `problem.h`).
- **`misc/`**: Core filesystem utilities (`mke2fs`, `tune2fs`, `dumpe2fs`, `e2image`, `e2undo`, `badblocks`, `chattr`, `lsattr`, `e4crypt`, `e2freefrag`, `filefrag`, `mklost+found`).
- **`debugfs/`**: Interactive and scripted filesystem debugger (`debugfs`, `ro_debugfs`). Also compiles shared source files into `e2fsck` and `misc`.
- **`resize/`**: Offline and online filesystem resizer (`resize2fs`).
- **`fuse4fs/`**: FUSE daemon for mounting ext2/ext3/ext4 filesystems in userspace (`fuse4fs`).
- **`scrub/`**: Online metadata scrubbing scripts and systemd/cron integration (`e2scrub`).
- **`tests/`**: Regression test suite (`tests/<prefix>_<name>/`) and test helper binaries (`tests/progs/`, `tests/fuzz/`).
- **`doc/`**: Documentation (`doc/libext2fs.texinfo`, `doc/RelNotes/`, and developer guides under `doc/developers/`).

---

## Building `e2fsprogs`

### Standard Build
`e2fsprogs` uses Autoconf and hand-written `Makefile.in` templates (with shared rules in `MCONFIG.in`). Both in-tree and out-of-tree builds are supported:

```sh
./configure
make -j$(nproc)
```

### Partial Rebuilds
- You can rebuild an individual directory with `make -C <dir>` (e.g., `make -C lib/ext2fs`, `make -C e2fsck`, `make -C misc`, `make -C debugfs`, `make -C resize`).
- By default (without `--enable-elf-shlibs`), utilities link statically against `lib/libext2fs.a` and other libraries in `lib/`. After modifying code under `lib/`, rebuild both the library directory and the dependent program directory (or run `make -j$(nproc)` at the top level).

### Warnings, Sanitizers, and Static Analysis
- **Treat warnings as errors (CI default)**:
  ```sh
  make -j$(nproc) V=1 CFLAGS_WARN="-Werror"
  ```
- **Strict compiler warning sweep**:
  ```sh
  make gcc-wall          # cleans and builds with extensive -W* flags
  make gcc-wall-new      # incremental build with strict -W* flags
  ```
- **Static analysis (`sparse` and `cppcheck`)**:
  ```sh
  make C=1 V=1           # runs sparse and cppcheck alongside compilation
  make C=2 V=1           # adds -Wbitwise -D__CHECK_ENDIAN__ endianness checks
  ```
- **Sanitizers (ASAN / UBSAN)**:
  ```sh
  ./configure CC=clang CFLAGS="-O2 -g -Wall -fsanitize=address,undefined -fno-sanitize-recover=address,undefined" \
              LDFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=address,undefined"
  make -j$(nproc) check
  ```
- **Header dependencies**: If you add or remove `#include` directives, run `make depend` to refresh the dependency lists at the bottom of `Makefile.in`.

---

## Running Regression Tests (`make check`)

### Running the Full Test Suite
Always verify changes by running `make check` (supports parallel execution with `-j`):

```sh
make -j$(nproc) check
```

Running `make check` from the top level:
1. Builds all libraries, utilities, and documentation (`make all`).
2. Builds and runs **unit test binaries** in each subdirectory:
   - `lib/ext2fs/`: `tst_bitops`, `tst_badblocks`, `tst_iscan`, `tst_types`, `tst_icount`, `tst_super_size`, `tst_inode_size`, `tst_csum`, `tst_crc32c`, `tst_bitmaps`, `tst_inline`, `tst_inline_data`, `tst_sha256`, `tst_sha512`, `tst_digest_encode`, etc.
   - `e2fsck/`: `tst_refcount`, `tst_region`, `tst_problem` (validates `problem_table` ordering and flags)
   - `debugfs/`: `tst_set_fields`
   - `lib/e2p/`: `tst_ostype`, `tst_feature`
   - `lib/uuid/`: `tst_uuid`
   - `lib/blkid/`: `tst_cache`, `tst_dev`, `tst_devname`, `tst_devno`, `tst_getsize`, `tst_probe`, `tst_read`, `tst_resolve`, `tst_save`, `tst_tag`, `tst_types`
   - `lib/ss/`: `test_ss`
   - `resize/`: `test_extent`
3. Runs the **regression test suite** in `tests/` (`tests/[a-z]_*`). By default, `make check` passes `--skip-slow-tests` to skip tests containing an `is_slow_test` marker file.

To run all tests **including slow tests**:

```sh
make -j$(nproc) fullcheck
```

### Running Individual Tests or Subsets in `tests/`
- **Run only the `tests/` directory in parallel**:
  ```sh
  make -C tests -j$(nproc) check
  ```
- **Run a single test or category via `test_script`**:
  ```sh
  make -C tests test_script
  (cd tests && ./test_script f_extent_oobounds)           # Run a single test
  (cd tests && ./test_script f)                           # Run all f_* (e2fsck) tests
  (cd tests && ./test_script --valgrind f_extent_oobounds) # Run under valgrind
  ```
- **Re-run only failed tests**:
  ```sh
  make -C tests check-failed
  # or:
  (cd tests && ./test_script --failed)
  ```
- **Inspecting test outputs**:
  - Passing a test creates `tests/<test_name>.ok`.
  - Failing a test creates `tests/<test_name>.failed`, which contains the unified diff between the expected golden output (`tests/<test_name>/expect` or `expect.1`/`expect.2`) and the actual filtered log (`tests/<test_name>.log` or `<test_name>.1.log`/`<test_name>.2.log`).

### Test Categories (`tests/<prefix>_<name>/`)
- `f_*`: `e2fsck` filesystem corruption detection and repair tests
- `j_*`: Journal recovery, replay, and corruption tests (`e2fsck` / `debugfs`)
- `d_*`: `debugfs` and `dumpe2fs` functional tests
- `m_*`: `mke2fs` filesystem creation tests
- `r_*`: `resize2fs` offline resizing tests
- `t_*`: `tune2fs` functional tests
- `i_*`: `e2image` metadata image tests
- `u_*`: `e2undo` undo-log tests
- `e_*`: `libext2fs` data structure (`icount`, `irel`, `brel`) tests

### Writing Unit Tests, Reproducers, and New Regression Tests
Before writing a unit test, creating a bug reproducer, or adding a new regression test, read:
- [`doc/developers/writing-unit-tests.md`](doc/developers/writing-unit-tests.md) — Guide to designing and implementing C and `libss` command-driven unit tests (such as `tst_bitmaps`, `tst_extents`, `tst_libext2fs`, and other `tst_*` binaries in `lib/ext2fs/`).
- [`doc/developers/create-a-regression-test.md`](doc/developers/create-a-regression-test.md) — Step-by-step instructions, script templates (`run_e2fsck`, `run_mke2fs`, custom scripts), determinism rules, and verification steps.
- [`doc/developers/create-a-reproducer.md`](doc/developers/create-a-reproducer.md) — Guide to synthesizing minimal ext4 images (`64 KB`–`1 MB`) and crafting on-disk corruptions from scratch using `mke2fs` and `debugfs` (without root or loop mounts).

Key rules for regression tests:
1. Prefer programmatic image generation (`$MKE2FS` + `$DEBUGFS` with `SKIP_GUNZIP="true"`) over checking in binary `image.gz` files.
2. Keep test images small (`-b 1024`, `100`–`1024` blocks) and always pass `-F -o Linux` to `$MKE2FS`.
3. `tests/<prefix>_<name>/script` is **sourced** by `test_one`: always use `return 0` (never `exit 0`) when skipping a test, and `unset` custom shell variables at the end of `script`.

---

## Coding Conventions

### C Formatting & Style (Linux Kernel K&R Style)
- **Indentation**: Use **8-character hard tabs** (`\t`). Do not use spaces for indentation. Continuation lines should be indented with tabs (using spaces only for final alignment to an opening parenthesis).
- **Line width**: Target 80 columns maximum where practical.
- **Braces**:
  - Place the opening brace `{` on the **same line** for control statements (`if`, `else`, `for`, `while`, `switch`) and `struct`/`union`/`enum` definitions.
  - Place the opening brace `{` on a **new line at column 0** for function definitions.
  - Omit braces around single-statement `if`/`for`/`while` bodies unless another branch of the same `if`/`else` chain requires braces.
- **Spacing**:
  - Put a space after keywords (`if (`, `for (`, `while (`, `switch (`, `return `), and around binary/assignment operators (`=`, `+`, `==`, `&`, `|`).
  - Do not put a space between a function name and its opening parenthesis `foo(a, b)`.
  - Attach pointer asterisks to the variable or function name (`char *buf`, not `char* buf`).
- **Comments**: Use `/* ... */` C-style comments. Multi-line comments use:
  ```c
  /*
   * Multi-line comment text...
   */
  ```
- **Naming**:
  - Functions, variables, and struct/type names: `lower_snake_case`.
  - Macros, constants, and enum/flag identifiers: `UPPER_SNAKE_CASE`.
  - Public `libext2fs` symbols: prefixed with `ext2fs_` (declared in `lib/ext2fs/ext2fs.h`); internal `libext2fs` declarations go in `lib/ext2fs/ext2fsP.h`.

### Language Standard & Portability
- **Standard**: GNU99 / C99 (`-std=gnu99`). Place variable declarations at the start of a block/function (avoid mixing declarations and statements mid-block) and never use variable-length arrays (VLAs).
- **Cross-platform portability**: `e2fsprogs` is built and tested across Linux (glibc, musl, dietlibc, Android Bionic), macOS, FreeBSD/NetBSD/OpenBSD, GNU/Hurd, and Windows (MinGW for `mke2fs` and libraries).
  - Always `#include "config.h"` as the very first header in every `.c` file.
  - Guard optional system headers with `#ifdef HAVE_<HEADER>_H` (e.g., `#ifdef HAVE_UNISTD_H`, `#ifdef HAVE_ERRNO_H`, `#ifdef HAVE_GETOPT_H`, `#ifdef HAVE_SYS_IOCTL_H`).
- **Prototypes and unused parameters**:
  - Mark file-local functions and globals `static`.
  - Every non-static function must have a declaration in an included header (`-Wstrict-prototypes -Wmissing-prototypes`).
  - Annotate intentionally unused function parameters with `EXT2FS_ATTR((unused))`.

### `e2fsprogs` API & Architecture Conventions
- **64-bit block numbers & types**:
  - Always use 64-bit block types (`blk64_t`, `e2_blkcnt_t`, `ext2_off64_t`) and 64-bit `libext2fs` routines (e.g., `ext2fs_block_iterate3`, `ext2fs_bmap2`, `ext2fs_test_block_bitmap2`, `ext2fs_mark_block_bitmap2`, `ext2fs_block_alloc_stats2`, `ext2fs_new_block2`) rather than legacy 32-bit `blk_t` APIs.
  - Use `ext2_ino_t` for inode numbers and `dgrp_t` for block group numbers.
- **ABI stability in shared libraries**:
  - Never change the signature or behavior of an existing exported function in `libext2fs`, `libe2p`, `libcom_err`, or `libss`. To add parameters or flags, introduce a new function (e.g., `ext2fs_foo2()`) and implement the older function as a compatibility wrapper.
  - Add new `libext2fs` error codes to the end of `lib/ext2fs/ext2_err.et.in`.
- **Endianness**:
  - On-disk ext2/3/4 structures are little-endian. Always use `ext2fs_le16_to_cpu()`, `ext2fs_le32_to_cpu()`, `ext2fs_le64_to_cpu()`, `ext2fs_cpu_to_le16()`, `ext2fs_cpu_to_le32()`, `ext2fs_cpu_to_le64()` (or `ext2fs_swab*`) when reading or writing raw on-disk structures that are not already swapped by `libext2fs` helpers.
- **Memory management & error handling**:
  - Prefer `ext2fs_get_mem()`, `ext2fs_get_memzero()`, `ext2fs_get_array()`, `ext2fs_resize_mem()`, and `ext2fs_free_mem()` over raw `malloc()`/`calloc()`/`free()`.
  - Functions in `libext2fs` return `errcode_t` (`0` on success). Use a single `errout:` / `cleanup:` label at the end of a function (`goto errout;`) to release allocated buffers and handles cleanly on failure.
- Never use printf to report errors in most libraries in e2fsprogs.  The only exception to this is `libsupport` which is used for those shared code which is not mature enough for formal shared libraries such as `libext2fs`.>
- **`e2fsck` problem reporting**:
  - Do not use `printf()` to report filesystem corruption in `e2fsck`. Define or use a `PR_<PASS>_*` problem code in `e2fsck/problem.h` and `e2fsck/problem.c` and invoke `fix_problem(ctx, PR_..., &pctx)`.
  - Problem strings in `e2fsck/problem.c` use `@`-expansions (`@i` = inode, `@b` = block, `@g` = group, `@S` = superblock, `@f` = filesystem, `@d` = directory, `@e` = entry, etc.) and must remain sorted in `problem_table` by problem code (verified by `tst_problem` during `make check`).

### Commit Messages & Patches
- Prefix commit subjects with the subsystem name (e.g., `e2fsck: ...`, `libext2fs: ...`, `debugfs: ...`, `mke2fs: ...`, `tune2fs: ...`, `resize2fs: ...`, `tests: ...`).
- Wrap commit message bodies at ~72 characters and explain both the root cause and the fix.
- **NEVER add a `Signed-off-by:` tag**: AI agents cannot legally certify the Developer Certificate of Origin (DCO). Do **not** generate or append a `Signed-off-by:` line for yourself or on behalf of the human user. The human submitter will review the commit and add their own `Signed-off-by:` tag.

## Mandatory AI Coding Assistant Rules
- When fixing a bug, include a minimal regression test which fails before the bug was fixed, and which passes afterwards.
