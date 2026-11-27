---
name: writing-unit-tests
description: Design, implement, and verify C and libss command-driven unit tests in e2fsprogs (such as tst_bitmaps / tst_bitmaps_cmds / tst_bitmaps_exp, tst_extents, tst_libext2fs, and tst_*.c programs in lib/ext2fs/). Use when asked to write, extend, or run unit tests for libext2fs, libe2p, libsupport, libss, e2fsck, debugfs, or resize2fs.
---

# Writing `e2fsprogs` Unit Tests

When designing, implementing, or extending a unit test in `e2fsprogs` (especially under `lib/ext2fs/`), read and follow the complete guide in [`doc/developers/writing-unit-tests.md`](../../../doc/developers/writing-unit-tests.md).

## Required Reading

Before writing any unit test code or command scripts, read:

1. `doc/developers/writing-unit-tests.md` — Complete unit testing principles, architectural patterns (`libss` command-driven tests, `debugfs`-based `tst_extents`/`tst_libext2fs` harnesses, standalone `tst_*.c` binaries, and `#ifdef UNITTEST`/`#ifdef DEBUG` self-tests), `Makefile.in` rules, and verification steps.
2. `AGENTS.md` — `e2fsprogs` build system, C coding conventions, 64-bit `libext2fs` API rules, and commit guidelines.

## Workflow Summary

1. **Choose the appropriate unit test pattern** (see Section 2 of `doc/developers/writing-unit-tests.md`):
   - **Pattern A (`libss` command-driven `tst_<name>` + `tst_<name>_cmd.ct` + `tst_<name>_cmds` + `tst_<name>_exp`)**: Modeled on `lib/ext2fs/tst_bitmaps.c`. Ideal for stateful APIs (bitmaps, allocators, containers, extent operations) where a command script and golden output transcript can also be run across multiple backends/flags.
   - **Pattern B (`debugfs`-based `tst_extents` / `tst_libext2fs` harnesses)**: Ideal for exercising `extent.c` (`extent_open` and `extent_cmds` in `debugfs/extent_inode.c`) or hooking new `libext2fs` test commands into `lib/ext2fs/tst_cmds.ct` and `lib/ext2fs/tst_libext2fs.c`.
   - **Pattern C (Standalone `tst_<name>.c` binary)**: Modeled on `tst_bitops.c`, `tst_badblocks.c`, `tst_iscan.c`, and `tst_super_size.c`.
   - **Pattern D (Single-file `#ifdef DEBUG` / `#ifdef UNITTEST` self-test)**: Modeled on `icount.c` (`tst_icount`), `csum.c` (`tst_csum`), `inline_data.c` (`tst_inline_data`), and `digest_encode.c` (`tst_digest_encode`).

2. **Apply core unit testing principles** (see Section 1 of `doc/developers/writing-unit-tests.md`):
   - Test behaviors, boundary/edge cases, and error paths (`errcode_t`), not internal implementation details.
   - Keep tests focused and DAMP: use helpers like `setup_filesystem(..., test_io_manager, &test_fs)` to hide boilerplate while passing test-relevant values explicitly.
   - Use non-zero, distinct argument values and cross-check complementary APIs when mutating state.
   - Ensure 100% deterministic output (no unpinned timestamps, random UUIDs, or raw `%p` pointers in output compared against `*_exp` files).

3. **Integrate into `Makefile.in` and verify** (see Sections 2.5 and 3 of `doc/developers/writing-unit-tests.md`):
   - Link against static libraries (`$(STATIC_LIBEXT2FS)`, `$(STATIC_LIBSS)`, `$(STATIC_LIBE2P)`, `$(STATIC_LIBCOM_ERR)`) and prefix source/command/expect files with `$(srcdir)/` for out-of-tree (`VPATH`) builds.
   - Wire the test into `fullcheck check::` and `clean::` in `Makefile.in` (and `.gitignore`).
   - Build with `CFLAGS_WARN="-Werror"` and run `make -C <dir> check` to verify zero warnings and passing tests.
