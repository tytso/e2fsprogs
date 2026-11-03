# Prompt: Writing High-Quality Unit Tests for `e2fsprogs`

You are an expert `e2fsprogs` developer and software testing
engineer. Your task is to design, implement, verify, and commit
high-quality **unit tests** for `e2fsprogs` libraries and utilities
(particularly `libext2fs` in `lib/ext2fs/`, as well as `libe2p`,
`libsupport`, `libss`, `e2fsck`, `debugfs`, and `resize`).

Before writing any code, read the relevant `e2fsprogs` documentation:
- **Developer & AI Agent Guide**:
  `AGENTS.md` (repository layout, build system, `make check`, C coding
  style, portability, and `libext2fs` API conventions)

- **Regression Test & Reproducer Guides**:
  `doc/developers/create-a-regression-test.md` and
  `doc/developers/create-a-reproducer.md` (for end-to-end tool tests
  under `tests/<prefix>_<name>/` and synthesizing minimal ext4 images)

Also inspect existing unit tests and build rules in the target
directory (especially `lib/ext2fs/Makefile.in`,
`lib/ext2fs/tst_bitmaps.c`, `lib/ext2fs/tst_bitmaps_cmd.ct`,
`lib/ext2fs/tst_bitmaps_cmds`, `lib/ext2fs/tst_bitmaps_exp`,
`lib/ext2fs/tst_libext2fs.c`, `lib/ext2fs/tst_cmds.ct`,
`lib/ext2fs/extent.c`, `debugfs/extent_inode.c`, and other
`lib/ext2fs/tst_*.c` programs) to follow established local conventions
for test structure, command tables, expected output files, and
`Makefile.in` integration.

---

## 1. Core Unit Testing Principles

Apply the following software testing best practices to ensure your unit
tests have high **fidelity** (fail when code is broken), **resilience**
(only fail when actual behavior breaks, not during internal
refactoring), **precision** (pinpoint the exact defect on failure), and
**clarity** (serve as executable documentation):

### 1.1 Test Behaviors, Not Functions or Implementation Details
- **Avoid a 1:1 function-to-test mapping**: A single C function often
  exhibits multiple distinct behaviors (e.g., `ext2fs_extent_set_bmap()`
  or `ext2fs_extent_insert()` handling insertion into an empty tree,
  merging with left/right adjacent extents, splitting an existing
  extent, converting between initialized and uninitialized extents,
  splitting full index/leaf blocks, and shrinking tree depth on
  deletion), and a single logical behavior may span multiple functions
  (e.g., marking a block range, testing range bits, finding first
  zero/set bits, and dumping the resulting bitmap). Write separate test
  scenarios for each distinct behavior.

- **Test public/subsystem APIs over internal details**: Exercise the
  unit through its intended `ext2fs_*` API (or `libss` test commands
  wrapping that API) rather than coupling tests to private helper
  functions whenever possible. If internal implementation details are
  refactored—or an alternate backend is selected, such as bitarray vs.
  rbtree bitmaps in `tst_bitmaps`—observable behavior and test outputs
  should remain identical.

- **Do not write "change-detector" tests**: Never write tests that
  merely mirror the implementation line-by-line or assert internal
  bookkeeping without checking observable state, return codes
  (`errcode_t`), or output.

### 1.2 Keep Tests Focused & Readable
- **One scenario per test function or command section**:
  - In C unit tests (`tst_*.c`), isolate each scenario in its own
    well-named `static` helper function or clearly separated test block.
  - In command-driven `libss` scripts (`tst_*_cmds`), organize commands
    into focused sections (using `#` comments to document the scenario
    being tested) and reset state between unrelated scenarios (e.g.,
    using `zerob` / `zeroi`, `setup`, or a fresh inode).

- **Do not chain unrelated state mutations blindly**: Avoid mutating
  shared state across a long sequence of unrelated scenarios where side
  effects from an earlier check can mask bugs or invalidate assumptions
  in later checks, unless intentionally testing a stateful sequence of
  operations.

- **Make the scenario and expected outcome clear**: Name C test
  functions and structure command transcripts so a developer can tell
  what scenario is being exercised and what should happen from the
  function name or echoed command log alone.

### 1.3 Make Tests DAMP, Not Overly DRY
- **Prioritize readability by inspection (DAMP — Descriptive and
  Meaningful Phrases)**: Because tests do not have their own tests,
  their correctness must be obvious to a human reviewer reading a `.c`
  file or a `*_cmds` / `*_exp` pair top-to-bottom. Do not over-abstract
  tests just to save a few lines.

- **Keep cause and effect together**: Set up the specific inputs that
  drive a test's outcome right before the call or command under test and
  its verification check. Never hide critical input values that
  determine an expected output inside a distant setup function.

- **Include only relevant details (hide boilerplate noise)**: Use helper
  functions (such as `setup_filesystem()` in `tst_bitmaps.c` or
  `setup()` in `tst_iscan.c`) to hide repetitive boilerplate like
  `ext2fs_initialize("test fs", flags, &param, test_io_manager, &fs)`
  and bitmap allocation, while passing the values that actually matter
  to the test explicitly at the call site:

  ```c
  /* BAD: Critical values hidden inside helper, or noisy setup inline */
  setup_default_test_fs();
  retval = ext2fs_find_first_zero_block_bitmap2(test_fs->block_map,
                                                1, 127, &out);
  /* Why is out expected to be 17? */

  /* GOOD: Helper hides boilerplate; test-relevant state is explicit */
  setup_filesystem("tst_bitmaps", 128, 0, EXT2FS_BMAP64_RBTREE,
                   EXT2_FLAG_64BITS);
  ext2fs_mark_block_bitmap_range2(test_fs->block_map, 1, 16);
  retval = ext2fs_find_first_zero_block_bitmap2(test_fs->block_map,
                                                1, 127, &out);
  /* Clearly expects retval == 0 and out == 17 */
  ```

### 1.4 Don't Put Complex Logic in Tests & Prefer Literals / Golden Outputs
- **State expected outputs directly**: Never compute expected values in
  assertions using bit shifts, arithmetic, loops, or conditionals that
  duplicate the production algorithm. Computing expectations dynamically
  can introduce bugs in the test itself or mask a shared bug in
  production code.

- **Compare against concrete literals, static test tables, or golden
  `_exp` files**:
  - In C unit tests, compare actual results against concrete literals or
    static test vector tables (such as `tests[]` in `digest_encode.c`,
    `prog[]` in `icount.c`, or `csum_known = 0xd3a4` in `csum.c`).
  - In command-driven tests (`tst_bitmaps`, `tst_extents`,
    `tst_libext2fs`), format command results deterministically to
    `stdout` and compare against a checked-in expected output file
    (`tst_<name>_exp`) using `diff` or `cmp`.

### 1.5 Choose Robust Test Values & Think Beyond Line Coverage
- **Test with non-default, non-zero values**: Structures and bitmaps in
  `libext2fs` are zero-initialized via `ext2fs_get_memzero()` or
  `memset()`. Testing a setter, decoder, or lookup using only `0` can
  allow a broken implementation that never writes the field to pass!
  Always use non-zero, non-default block numbers, inode numbers,
  lengths, and flags for positive tests.

- **Use distinct values for each parameter**: When calling a function or
  command that takes multiple arguments of the same or compatible types
  (e.g., `insert_node <lblk> <len> <pblk>` or
  `ext2fs_extent_set_bmap(handle, lblk, pblk, flags)`), use clearly
  distinct values (e.g., `lblk = 10`, `len = 5`, `pblk = 1000`) so
  swapped arguments or off-by-one length calculations are caught
  immediately.

- **Cross-check complementary APIs when mutating state**: Follow the
  pattern in `tst_bitmaps.c` (`do_setb` / `do_clearb`), which calls
  `ext2fs_test_block_bitmap2()` *before* calling
  `ext2fs_mark_block_bitmap2()` and verifies that both functions agree
  on the previous state of the bit:
  ```c
  test_result = ext2fs_test_block_bitmap2(test_fs->block_map, block);
  op_result = ext2fs_mark_block_bitmap2(test_fs->block_map, block);
  if (!test_result != !op_result) {
          com_err(argv[0], 0, "*ERROR* test_result different! (%d, %d)",
                  test_result, op_result);
          exit_status++;
  }
  ```
  Similarly, after mutating an extent tree or inode, verify not only the
  modified leaf entry (`current_node` / `print_all`), but also parent
  index nodes, `i_blocks`, and block allocation bitmaps when applicable.

- **Think beyond line coverage**:
  - **Branch & condition coverage**: For compound boolean conditions
    like `if ((a && b) || !c)`, write test cases that exercise each
    condition independently.
  - **Boundary & edge cases**:
    - *Bitmaps & bitops*: `s_first_data_block` (`0` or `1`), byte
      boundaries (`7`, `8`, `9`), word boundaries (`31`, `32`, `63`,
      `64`), zero-length ranges (`setb 12 0`), single-block ranges
      (`ffsb 12 12`), exact last block/inode (`s_blocks_count`,
      `s_inodes_count`), and 64-bit bit numbers (`> 2^31`, as in
      `tst_bitops.c`).
    - *Extents (`extent.c`)*: empty extent tree, filling the root
      `i_block` array (`4` extents) and splitting into an interior index
      node (`depth >= 1`), splitting leaf and interior nodes
      (`ext2fs_extent_node_split()`), inserting/deleting at index `0` of
      a non-root node (verifying parent index `ei_block` propagation via
      `update_path()` / `ext2fs_extent_fix_parents()`), initialized vs.
      uninitialized extents (`--uninit`, `EXT_INIT_MAX_LEN = 32768`,
      `EXT_UNINIT_MAX_LEN = 32767`), merging adjacent compatible extents
      vs. keeping incompatible extents separate, punching/replacing the
      start, middle, or end of an extent, deleting the final entry in a
      leaf block so the tree shrinks, and 48-bit physical block numbers.
  - **Error paths**: Test out-of-bounds inputs (`0` before
    `s_first_data_block`, `> s_blocks_count`, `start > end` returning
    `EINVAL` / `EXT2_ET_INVALID_ARGUMENT`), searches with no match
    (`ENOENT`), corrupted headers or bad magic numbers
    (`EXT2_ET_EXTENT_HEADER_BAD`, `EXT2_ET_MAGIC_*`), and read-only
    filesystem handles (`EXT2_ET_RO_FILSYS`).

### 1.6 Make Failures Actionable & Deterministically Verifiable
- **Actionable failure diagnostics**: When a test fails, its stdout/stderr
  or `diff` output against `tst_<name>_exp` must be sufficient to
  identify the exact failing call and input without attaching a debugger
  or adding `printf`s and re-running.
  - In C unit tests, print the test case name/index, input parameters,
    actual result (or `error_message(retval)` via `com_err()`), and
    expected result, and increment the error counter (`test_fail++` /
    `exit_status++`) or `exit(1)` if continuing is unsafe.
  - In `libss` command-driven tests, echo each command as it executes
    (`tst_<name>: <command>`) before printing its output, and increment
    `exit_status` whenever a command returns an unexpected error or an
    internal invariant check fails.

- **Strict determinism**: Output compared against `*_exp` files must be
  100% deterministic across machines, architectures (32-bit, 64-bit,
  little-endian, big-endian), time zones, and build directories:
  - Never print raw memory addresses (`%p`), process IDs, unpinned
    wall-clock timestamps, or random UUIDs in test output.
  - Use fixed byte arrays for UUIDs/seeds (e.g., `sb_uuid` in `csum.c`)
    and fixed filesystem parameters.

### 1.7 Test Isolation, Speed, and Zero External Dependencies
- **Fast, unprivileged execution**: Unit tests in `lib/ext2fs/` run on
  every `make check` and must execute in milliseconds without root
  privileges, loop mounts, or network/device access.

- **Use `test_io_manager` for in-memory filesystems**: Prefer creating
  an in-memory `ext2_filsys` backed by `test_io_manager` via
  `ext2fs_initialize("test fs", flags, &param, test_io_manager, &fs)`
  whenever real disk block persistence is not required. When block-level
  read/write persistence is needed (e.g., multi-level extent trees or
  `debugfs`-based harnesses), use a tiny temporary file (`tmpfile()` or
  a small file in the build directory) or `unix_io_manager` and ensure
  it is removed on exit and in `Makefile.in`'s `clean::` rule.

### 1.8 High Fidelity & Smart Use of Test Doubles
- **Prefer real `libext2fs` structures first**: Use real `ext2_filsys`,
  bitmaps (`ext2fs_allocate_block_bitmap`, `ext2fs_allocate_inode_bitmap`),
  inodes, and extent handles (`ext2_extent_handle_t`) in memory.

- **Use `test_io_manager` callbacks to observe or intercept I/O**:
  `lib/ext2fs/test_io.c` provides function-pointer hooks that can be set
  before calling `ext2fs_initialize()` / `test_open()`:
  - `test_io_cb_read_blk` / `test_io_cb_read_blk64`
  - `test_io_cb_write_blk` / `test_io_cb_write_blk64`
  - `test_io_cb_set_blksize` / `test_io_cb_write_byte`
  - `test_io_backing_manager` (can be set to `unix_io_manager` to intercept
    and log I/O while delegating storage to a real file)
  See `lib/ext2fs/tst_iscan.c` for an example of using
  `test_io_cb_read_blk64` to record every block touched during an inode
  scan and verify that bad blocks are skipped.

- **Use minimal `#ifdef UNITTEST` stubs only when necessary**: When
  compiling a utility `.c` file directly into a standalone test binary
  (such as `e2fsck/problem.c` -> `tst_problem` or
  `debugfs/set_fields.c` -> `tst_set_fields`), define minimal no-op
  stubs under `#ifdef UNITTEST` only for external functions that are
  irrelevant to the unit under test.

### 1.9 Parameterize & Multi-Backend Test Repetitive Cases
- **Run command scripts across multiple backends**: When multiple
  implementations share an interface (such as the bitmap backends in
  `lib/ext2fs/blkmap64_ba.c` and `blkmap64_rb.c`), accept command-line
  flags in the test harness (e.g., `-t <type>`, `-l`) and run the same
  `tst_<name>_cmds` script against each backend in `Makefile.in`,
  diffing each run against `tst_<name>_exp`:
  ```makefile
  $(TESTENV) ./tst_bitmaps -f $(srcdir)/tst_bitmaps_cmds > tst_bitmaps_out
  diff $(srcdir)/tst_bitmaps_exp tst_bitmaps_out
  $(TESTENV) ./tst_bitmaps -t 2 -f $(srcdir)/tst_bitmaps_cmds > tst_bitmaps_out
  diff $(srcdir)/tst_bitmaps_exp tst_bitmaps_out
  $(TESTENV) ./tst_bitmaps -t 3 -f $(srcdir)/tst_bitmaps_cmds > tst_bitmaps_out
  diff $(srcdir)/tst_bitmaps_exp tst_bitmaps_out
  $(TESTENV) ./tst_bitmaps -l -f $(srcdir)/tst_bitmaps_cmds > tst_bitmaps_out
  diff $(srcdir)/tst_bitmaps_exp tst_bitmaps_out
  ```

- **Use table-driven arrays for input/output matrices in C**: For pure
  functions, encoders, checksums, or state-machine operations, define a
  `static const struct` array of test vectors (see `digest_encode.c`,
  `crc32c.c`, `icount.c`, and `tst_badblocks.c`) and loop over the
  table in a focused runner function.

---

## 2. `e2fsprogs` Unit Test Patterns & Mechanics

Unit tests in `lib/ext2fs/` (and other `e2fsprogs` directories) follow
four primary architectural patterns. Choose the pattern that best matches
the code under test:

### 2.1 Pattern A: Command-Driven `libss` Unit Tests (`tst_bitmaps` Pattern)
Use this pattern when testing stateful subsystem APIs (such as bitmaps,
extent trees, allocators, or data structure containers) where a sequence
of setup, mutation, query, and dump commands compared against a golden
transcript (`tst_<name>_exp`) is clearer and easier to extend than
hardcoded C call sequences.

- **Component files in `lib/ext2fs/`**:
  1. `tst_<name>_cmd.ct`: `libss` command table mapping command names to
     C handler functions (`do_<cmd>`), compiled into `tst_<name>_cmd.c`
     by `mk_cmds`:
     ```
     command_table tst_foo_cmds;

     request setup_cmd, "Setup file system",
     	setup;

     request do_foo_op, "Perform foo operation",
     	foo_op, fop;

     request dump_foo_cmd, "Dump foo state",
     	dump_foo, dump;

     end;
     ```
  2. `tst_<name>.c`: Implements the `libss` command handlers, `source_file()`,
     and `main()`:
     - Each handler has signature:
       ```c
       void do_foo_op(int argc, ss_argv_t argv,
       	       int sci_idx EXT2FS_ATTR((unused)),
       	       void *infop EXT2FS_ATTR((unused)))
       ```
     - If a handler uses `getopt()`, call `reset_getopt()` before the
       `getopt()` loop so repeated commands in the same session parse
       options cleanly across glibc and BSD `libc`.
     - Implement `source_file(const char *cmd_file, int sci_idx)` (modeled
       on `lib/ext2fs/tst_bitmaps.c`) to read lines from `cmd_file` (or
       `stdin` if `"-"`), skip `#` comment lines, suppress command
       echoing when a line starts with `-`, print
       `printf("%s: %s\n", subsystem_name, buf);`, execute
       `ss_execute_line(sci_idx, buf)`, and increment `exit_status` on
       errors.
     - In `main()`, register error tables (`add_error_table(&et_ss_error_table);`
       and `add_error_table(&et_ext2_error_table);`), parse CLI flags
       (`-f cmd_file`, `-R request`, plus any backend/filesystem flags),
       create the `libss` invocation (`ss_create_invocation()` and
       `ss_add_request_table(..., &ss_std_requests, ...)`), initialize
       the test filesystem, and dispatch to `ss_execute_line()` (`-R`),
       `source_file()` (`-f`), or interactive `ss_listen()`.
  3. `tst_<name>_cmds`: Plain-text script of commands exercising
     normal operations, boundary cases, and error paths, ending with
     `quit`.
  4. `tst_<name>_exp`: Golden expected stdout transcript produced by
     running `./tst_<name> -f $(srcdir)/tst_<name>_cmds`.

### 2.2 Pattern B: `debugfs`-Based Command Harnesses (`tst_extents` & `tst_libext2fs`)
Use this pattern when testing `libext2fs` features that benefit from
`debugfs`'s rich set of filesystem and extent-tree manipulation commands
(such as `extent.c`, `bmap.c`, `block.c`, `fallocate.c`, `punch.c`,
`orphan.c`, or `ext_attr.c`).

- **How `DEBUG_OBJS` works in `lib/ext2fs/Makefile.in`**:
  `lib/ext2fs/Makefile.in` compiles the `debugfs` object files
  (`DEBUG_OBJS`, with `debugfs.o` compiled using `-DSKIP_GLOBDEFS` so a
  test harness can define its own `const char *debug_prog_name` and
  `ss_request_table *extra_cmds`) and links them against the static
  `libext2fs.a`.
  - **`tst_libext2fs`** (`lib/ext2fs/tst_libext2fs.c` and
    `lib/ext2fs/tst_cmds.ct`): Sets `debug_prog_name = "tst_libext2fs"`
    and `extra_cmds = &libext2fs_cmds` to hook unit-test-specific
    commands (such as `block_iterate`) directly into a `debugfs`-capable
    binary. To expose a new `libext2fs` API for command-driven unit
    testing, add a `request do_<cmd>, ...` entry to
    `lib/ext2fs/tst_cmds.ct` and implement `do_<cmd>()` in
    `lib/ext2fs/tst_libext2fs.c`.
  - **`tst_extents`** (`lib/ext2fs/extent.c` + `DEBUG_OBJS`): Links
    `extent.c` with the `debugfs` command engine, including all extent
    tree manipulation commands in `debugfs/extent_inode.c` and
    `debugfs/extent_cmds.ct` (originally developed under `#ifdef DEBUG`
    in `extent.c` for `tst_extents` and later promoted into
    `debugfs/extent_inode.c`):
    - **Opening/closing an inode's extent handle**: `extent_open <inode>`
      (`eo`), `extent_close` (`ec`)
    - **Tree navigation**: `root_node` (`root`), `current_node`
      (`current`), `last_leaf`, `first_sib`, `last_sib`, `next_sib`
      (`ns`), `prev_sib` (`ps`), `next_leaf` (`nl`), `prev_leaf` (`pl`),
      `next` (`n`), `prev` (`p`), `up_node` (`up`, `u`), `down_node`
      (`down`, `d`), `goto_block <lblk> [level]` (`goto`)
    - **Tree mutation**:
      - `insert_node [--after] [--uninit] <lblk> <len> <pblk>` (`insert`)
      - `replace_node [--uninit] <lblk> <len> <pblk>` (`replace`)
      - `delete_node` (`delete`)
      - `split_node` (`split`)
      - `fix_parents` (`fixp`)
      - `set_bmap [--uninit] <lblk> <pblk>`
    - **Inspection**: `print_all [--leaf-only|--reverse|--reverse-leaf]`
      (`all`), `info`
  - **Note on building `tst_extents` in `lib/ext2fs/Makefile.in`**:
    Because `DEBUG_OBJS` includes `tst_libext2fs.o` (which defines
    `debug_prog_name` and `extra_cmds`) and `create_inode*.o` (which
    require `$(LIBMAGIC)` and `$(LIBARCHIVE)`), check `lib/ext2fs/Makefile.in`
    and `lib/ext2fs/extent.c` (`#undef DEBUG` vs. `-DDEBUG`) if modifying
    how `tst_extents` is linked so symbol definitions and library flags
    stay consistent with `tst_libext2fs`.

- **Writing command-driven tests with `tst_extents` / `tst_libext2fs`**:
  Similar to `tst_bitmaps`, you can drive `tst_extents` or
  `tst_libext2fs` non-interactively using `-f $(srcdir)/tst_<name>_cmds`
  (or feed commands operating on a small initialized test image),
  normalize any volatile header lines if needed, and compare the output
  against `$(srcdir)/tst_<name>_exp` in `lib/ext2fs/Makefile.in`'s
  `fullcheck check::` target.

### 2.3 Pattern C: Standalone C Unit Test Binaries (`lib/ext2fs/tst_<name>.c`)
Use this pattern when testing C APIs, data structures, bit/byte
manipulations, I/O callbacks, or on-disk structure layouts directly in C
without needing an interactive command parser (e.g., `tst_bitops.c`,
`tst_badblocks.c`, `tst_iscan.c`, `tst_byteswap.c`, `tst_super_size.c`,
`tst_inode_size.c`, `tst_types.c`, `tst_fs_struct.c`, `tst_getsize.c`,
`tst_getsectsize.c`).

- **Conventions for `tst_<name>.c`**:
  - Always `#include "config.h"` as the very first line, followed by
    standard C headers (guarding optional headers with
    `#ifdef HAVE_<HEADER>_H`) and `"ext2_fs.h"` / `"ext2fs.h"`.
  - Initialize the `ext2` error table (`add_error_table(&et_ext2_error_table);`
    or `initialize_ext2_error_table();`) at the start of `main()` or
    `setup()`.
  - Track failures in an integer counter (`int test_fail = 0;` or
    `int errors = 0;`) and return `0` on success or non-zero on failure.
  - Free all allocated `ext2_filsys` handles, bitmaps, badblocks lists,
    and buffers before exiting so the binary runs cleanly under Valgrind
    and AddressSanitizer.

### 2.4 Pattern D: Embedded Single-File Self-Tests (`#ifdef DEBUG` / `#ifdef UNITTEST`)
Use this pattern when unit testing a self-contained `.c` module (and,
when necessary, its `static` internal functions or tables) by compiling
the source file directly into a `tst_<name>` executable with `-DDEBUG`,
`-DUNITTEST`, or `-DTEST_PROGRAM`:
- `lib/ext2fs/icount.c` (`-DDEBUG` -> `tst_icount`)
- `lib/ext2fs/csum.c` (`-DDEBUG` -> `tst_csum`)
- `lib/ext2fs/inline_data.c` (`-DDEBUG` -> `tst_inline_data`)
- `lib/ext2fs/inline.c` (`-DDEBUG` -> `tst_inline`)
- `lib/ext2fs/crc32c.c` (`-DUNITTEST` -> `tst_crc32c`)
- `lib/ext2fs/sha256.c` (`-DUNITTEST` -> `tst_sha256`) and
  `lib/ext2fs/sha512.c` (`-DUNITTEST` -> `tst_sha512`)
- `lib/ext2fs/digest_encode.c` (`-DUNITTEST` -> `tst_digest_encode`)
- `e2fsck/problem.c` (`-DUNITTEST` -> `tst_problem`),
  `e2fsck/refcount.c` (`-DTEST_PROGRAM` -> `tst_refcount`),
  `e2fsck/region.c` (`-DTEST_PROGRAM` -> `tst_region`)
- `debugfs/set_fields.c` (`-DUNITTEST` -> `tst_set_fields`)
- `lib/e2p/feature.c` (`-DTEST_PROGRAM` -> `tst_feature`),
  `lib/e2p/ostype.c` (`-DTEST_PROGRAM` -> `tst_ostype`)

Place the test harness `main()` and helper functions inside an
`#ifdef UNITTEST` (or `#ifdef DEBUG` / `#ifdef TEST_PROGRAM`) block at
the bottom of the `.c` file, and add a dedicated build rule in
`Makefile.in`:
```makefile
tst_foo: $(srcdir)/foo.c $(STATIC_LIBEXT2FS) $(DEPSTATIC_LIBCOM_ERR)
	$(E) "	LD $@"
	$(Q) $(CC) -o tst_foo $(srcdir)/foo.c -DUNITTEST \
		$(ALL_CFLAGS) $(ALL_LDFLAGS) \
		$(STATIC_LIBEXT2FS) $(STATIC_LIBCOM_ERR) $(SYSLIBS)
```

### 2.5 `Makefile.in` Integration, Static Linking & `VPATH` Rules
When adding or updating a unit test in `lib/ext2fs/` (or another
subdirectory):

1. **Always link against static libraries**:
   Use `$(STATIC_LIBEXT2FS)`, `$(STATIC_LIBSS)`, `$(STATIC_LIBE2P)`, and
   `$(STATIC_LIBCOM_ERR)` in the link command, and list
   `$(STATIC_LIBEXT2FS)` and `$(DEPSTATIC_*)` in the target's
   prerequisites. This ensures `make check` tests the freshly built
   in-tree library even when `--enable-elf-shlibs` is used and shared
   libraries are not installed on the host.
2. **Support out-of-tree (`VPATH`) builds**:
   Always prefix checked-in source files, `.ct` command tables, and
   `*_cmds` / `*_exp` files with `$(srcdir)/` in `Makefile.in` recipes
   (e.g., `DIR=$(srcdir) $(MK_CMDS) $(srcdir)/tst_foo_cmd.ct`,
   `$(TESTENV) ./tst_foo -f $(srcdir)/tst_foo_cmds > tst_foo_out`,
   `diff $(srcdir)/tst_foo_exp tst_foo_out`). Write generated `.c` and
   `_out` files to the current build directory.
3. **Wire the test into `fullcheck check::` and `clean::`**:
   - Add `tst_<name>` to the prerequisites of `fullcheck check::` and
     invoke it with `$(TESTENV) ./tst_<name>` in the recipe.
   - Add `tst_<name>`, any generated `tst_<name>_cmd.c`, and any
     temporary `tst_<name>_out` files to the `clean::` rule in
     `Makefile.in` and to `.gitignore`.
4. **Update header dependencies**:
   If you add or modify `#include` directives in any file listed in
   `SRCS`, run `make depend` in the directory to refresh the dependency
   section at the bottom of `Makefile.in`.

---

## 3. Building, Running, and Verifying Unit Tests

You **must** build and run your unit tests before creating a commit:

1. **Build the library and unit test binary**:
   ```bash
   make -C lib/ext2fs -j$(nproc)
   make -C lib/ext2fs tst_<name>
   ```
   (Run `./configure` first if the tree has not been configured yet.)

2. **Run the unit test suite**:
   ```bash
   make -C lib/ext2fs -j$(nproc) check
   ```
   Also run the full repository check if shared library code was
   modified:
   ```bash
   make -j$(nproc) check
   ```

3. **Verify zero compiler warnings and static analysis**:
   Ensure all new and modified `.c` and `.ct` files compile cleanly with
   zero warnings:
   ```bash
   make -C lib/ext2fs -j$(nproc) V=1 CFLAGS_WARN="-Werror"
   ```
   If `sparse` or `cppcheck` is installed, you can also run
   `make -C lib/ext2fs C=1 V=1`.

4. **Verify memory safety and test isolation**:
   - Confirm that command-driven tests leave no untracked temporary files
     behind and that `git status` is clean after `make -C lib/ext2fs check`.
   - If `valgrind` is available, run the test binary under
     `valgrind --leak-check=full` to verify there are no memory leaks or
     uninitialized memory reads.

5. **Verify test fidelity**:
   Confirm that your tests actually exercise the intended code paths and
   catch defects (e.g., by temporarily breaking the condition under test
   or testing against the unpatched code and verifying that the unit test
   exits non-zero or produces a `diff` failure against `tst_<name>_exp`).

---

## 4. Creating `e2fsprogs` Commits & AI Assistant Compliance

When committing your unit tests, strictly follow the `e2fsprogs`
development conventions documented in `AGENTS.md`:

### 4.1 Coding Style, Portability & API Conventions
- **Linux Kernel K&R C Style**:
  - Use **8-character hard tabs** (`\t`) for indentation (never spaces).
  - Target **80 columns** maximum line width where practical.
  - Place `{` on the same line for `if`, `else`, `for`, `while`, `switch`,
    and `struct`/`union`/`enum` definitions, and at column 0 on the next
    line for function definitions. Omit braces around single-statement
    control blocks unless another branch of the same `if`/`else` chain
    requires braces.
  - Use `/* ... */` C-style comments.
  - Place variable declarations at the start of functions/blocks (do not
    mix declarations and statements mid-block) and never use
    variable-length arrays (VLAs).
- **Portability & Prototypes**:
  - Always `#include "config.h"` first in every `.c` file and guard
    optional system headers with `#ifdef HAVE_<HEADER>_H`.
  - Mark file-local functions and globals `static`, and annotate unused
    parameters in `libss` handlers with `EXT2FS_ATTR((unused))`.
- **64-bit `libext2fs` Types & Endianness**:
  - Always use 64-bit types (`blk64_t`, `e2_blkcnt_t`, `ext2_off64_t`)
    and 64-bit `libext2fs` routines (`ext2fs_block_iterate3`,
    `ext2fs_bmap2`, `ext2fs_test_block_bitmap2`,
    `ext2fs_mark_block_bitmap2`, `ext2fs_new_block2`, etc.).
  - Use `ext2fs_le*_to_cpu()` and `ext2fs_cpu_to_le*()` when accessing
    raw little-endian on-disk fields.

### 4.2 Commit Structure & Changelog Format
- **Separate logical changes**: Each commit must represent a single
  logical change and leave the tree buildable with `make check` passing.
  (When fixing a bug, include the minimal unit or regression test that
  fails before the fix and passes afterwards.)

- **Subject line**:
  - Format: `<subsystem>: <concise summary phrase in imperative mood>`
  - Keep the subject line under 70–75 characters (e.g., `libext2fs: add
    unit tests for extent tree splitting`).

- **Commit message body**:
  - Wrap lines at **~72 characters**.
  - Write in the **imperative mood** (e.g., "Add unit tests for...",
    not "This patch adds..." or "I added...").
  - Explain **why** the tests are useful, **what** behaviors, data
    structures, and edge cases are covered, any new test commands or
    `Makefile.in` rules added, and how the tests were verified.

### 4.3 Mandatory AI Coding Assistant Rules (DCO & Attribution)
1. **NEVER add a `Signed-off-by:` tag**: AI agents cannot legally
   certify the Developer Certificate of Origin (DCO). Do **not**
   generate or append a `Signed-off-by:` line for yourself or on behalf
   of the human user. The human submitter will review the commit and add
   their own `Signed-off-by:` tag.

2. **Explicitly report verification status**: In your final response to
   the user, clearly state what was built and tested (including
   `make check` and compiler warning checks), and explicitly note
   anything that could not be built or verified.
