You are working in the `e2fsprogs` source tree (located in the current working directory `.`).

### Goal
Create a new, self-contained **regression test** in `./tests/<prefix>_<name>/` that tests a target bug fix, feature, or code path in `e2fsprogs` (or converts an existing reproducer such as one created from `doc/developers/create-a-reproducer.md`), following all `e2fsprogs` test suite conventions and passing `(cd tests && ./test_script <prefix>_<name>)`.

---

### Step 1: Understand the `./tests` Harness and Naming Conventions

1. **Test Directory Naming (`tests/<prefix>_<name>/`)**:
   Every test lives in its own subdirectory under `tests/` with a single-letter prefix indicating what it tests:
   - `f_*`: `e2fsck` filesystem corruption detection and repair tests (most common)
   - `j_*`: Journal replay, recovery, and corruption tests (`e2fsck` / `debugfs`)
   - `d_*`: `debugfs` and `dumpe2fs` functional tests
   - `m_*`: `mke2fs` filesystem creation tests
   - `r_*`: `resize2fs` offline resizing tests
   - `t_*`: `tune2fs` functional tests
   - `i_*`: `e2image` metadata image tests
   - `u_*`: `e2undo` undo-log tests
   - `e_*`: `libext2fs` data structure (`icount`, `irel`, `brel`) unit tests

2. **How `test_script` and `test_one` Execute Tests (`tests/test_script.in`, `tests/test_one.in` & `tests/test_config`)**:
   - **`./test_script` is the primary interface for manually running tests** from the `tests/` directory (generated from `tests/test_script.in` via `make -C tests test_script`):
     - Run a single test: `(cd tests && ./test_script <prefix>_<name>)`
     - Run all tests in a category: `(cd tests && ./test_script <prefix>)` (e.g., `./test_script f`)
     - Re-run failed tests: `(cd tests && ./test_script --failed)`
     - Run under valgrind: `(cd tests && ./test_script --valgrind <prefix>_<name>)`
     - `test_script` invokes `./test_one` for each selected test and then runs `./test_post` to summarize results and return a non-zero exit status if any test failed.
   - **`./test_one` is the low-level per-test helper** (generated from `tests/test_one.in`), used internally by `test_script` and by `make check` in `tests/` so that `make -j<N> check` can execute individual test targets in parallel.
   - When executing a test, `test_one` sources `tests/test_config`, which:
     - Defines tool variables (wrapping `$USE_VALGRIND` when `--valgrind` is passed): `$FSCK`, `$MKE2FS`, `$DEBUGFS`, `$DEBUGFS_EXE`, `$DUMPE2FS`, `$TUNE2FS`, `$CHATTR`, `$LSATTR`, `$E2IMAGE`, `$E2IMAGE_EXE`, `$RESIZE2FS`, `$RESIZE2FS_EXE`, `$E2UNDO`, `$E2UNDO_EXE`, `$CRCSUM`, `$CLEAN_OUTPUT` (`sed -f $cmd_dir/filter.sed`), and `$TEST_BITS` (`test_data.tmp`).
     - Exports deterministic environment settings: `TZ=GMT0`, `LC_ALL=C`, `E2FSCK_CONFIG=/dev/null`, `MKE2FS_CONFIG=./mke2fs.conf`, `BLKID_FILE=/dev/null`, `E2FSPROGS_SKIP_PROGRESS=yes`, `EXT2FS_NO_MTAB_OK=yes`, `RESIZE2FS_FORCE_LAZY_ITABLE_INIT=yes`, `E2FSPROGS_LIBMAGIC_SUPPRESS=yes`.
   - `test_one` sets `$test_dir`, `$test_name`, `$cmd_dir` (`$SRCDIR`), reads `$test_description` from `$test_dir/name`, creates a temporary file `$TMPFILE` (automatically removed on exit unless `SKIP_UNLINK="true"`), and **sources** `$test_dir/script` (or falls back to `$SRCDIR/defaults/${prefix}_script` if `$test_dir/script` does not exist).
   - **Important**: Because `$test_dir/script` is sourced (`. $test_dir/script`), always use `return 0` (never `exit 0`) when skipping a test early, and `unset` any custom shell variables at the end of `script`.

---

### Step 2: Choose the Test Pattern and Structure

Prefer **programmatic image generation** inside `tests/<prefix>_<name>/script` (with `SKIP_GUNZIP="true"`) over checking in binary `image.gz` files whenever the filesystem state can be synthesized with `mke2fs` and `debugfs`.

#### Pattern A: `e2fsck` Two-Pass Tests (`f_*` and `j_*` via `$cmd_dir/run_e2fsck`)
Used when testing `e2fsck` repair or journal recovery.
- **Files in `tests/<prefix>_<name>/`**:
  - `name`: A single line concisely describing the test (no trailing period).
  - `script`: Synthesizes the test image in `$TMPFILE` and sources `. $cmd_dir/run_e2fsck` (see template below).
  - `expect.1`: Expected filtered output of the first `e2fsck` pass (ending with `Exit status is 1` when errors are fixed, or `Exit status is 0` if only journal/orphan preen cleanup occurred).
  - `expect.2`: Expected filtered output of the second `e2fsck` pass verifying the repaired filesystem is clean (ending with `Exit status is 0`).
- **Template for `tests/f_<name>/script`** (modeled on `tests/f_extent_oobounds/script` and `tests/f_bigalloc_orphan_list/script`):
  ```sh
  if ! test -x $DEBUGFS_EXE; then
  	echo "$test_name: $test_description: skipped (no debugfs)"
  	return 0
  fi

  SKIP_GUNZIP="true"

  touch $TMPFILE
  $MKE2FS -Fq -t ext4 -o Linux -b 1024 $TMPFILE 256 > /dev/null 2>&1
  $DEBUGFS -w $TMPFILE << EOF > /dev/null 2>&1
  # debugfs commands to construct the on-disk state / corruption
  quit
  EOF

  . $cmd_dir/run_e2fsck
  ```
- **Customization variables supported by `$cmd_dir/run_e2fsck`**:
  - `SKIP_GUNZIP="true"`: Skip decompressing `$test_dir/image.gz` into `$TMPFILE`.
  - `FSCK_OPT` (default `-yf`) and `SECOND_FSCK_OPT` (default `-yf`): Override `e2fsck` options for Pass 1 or Pass 2.
  - `ONE_PASS_ONLY="true"`: Run only Pass 1 and compare only `expect.1` (no `expect.2`).
  - `PREP_CMD`: Shell command string `eval`-ed immediately before Pass 1.
  - `AFTER_CMD`: Shell command string `eval`-ed after Pass 2.
  - `PASS_ZERO="true"`: Also compare `$test_name.0.log` against `$test_dir/expect.0` (often paired with `PREP_CMD` or `AFTER_CMD` to capture `debugfs` or `dumpe2fs` output normalized via `sed -f $cmd_dir/filter.sed > $test_name.0.log`).

#### Pattern B: `mke2fs` Tests (`m_*` via `$cmd_dir/run_mke2fs`)
Used when testing `mke2fs` options and resulting filesystem layout (`expect.1` only):
```sh
DESCRIPTION="short description of mke2fs test"
FS_SIZE=1024
MKE2FS_OPTS="-t ext4 -O ..."
. $cmd_dir/run_mke2fs
```

#### Pattern C: Custom Command / Single-Output Tests (`d_*`, `t_*`, `r_*`, `i_*`, `u_*`, `j_*`)
Used when testing sequences of `debugfs`, `tune2fs`, `dumpe2fs`, `resize2fs`, `e2image`, or `e2undo` commands against a single `expect` file:
- **Files in `tests/<prefix>_<name>/`**: `name`, `script`, and `expect`.
- **Template for custom `script`**:
  ```sh
  if ! test -x $DEBUGFS_EXE; then
  	echo "$test_name: $test_description: skipped (no debugfs)"
  	return 0
  fi

  OUT=$test_name.log
  EXP=$test_dir/expect

  dd if=/dev/zero of=$TMPFILE bs=1k count=512 > /dev/null 2>&1
  $MKE2FS -Fq -t ext4 -o Linux -b 1024 $TMPFILE 512 > $OUT.new 2>&1

  # Run commands under test, appending output and exit status to $OUT.new
  $DEBUGFS -w -R "..." $TMPFILE >> $OUT.new 2>&1
  status=$?
  echo Exit status is $status >> $OUT.new

  $FSCK -fy -N test_filesys $TMPFILE >> $OUT.new 2>&1
  status=$?
  echo Exit status is $status >> $OUT.new

  sed -f $cmd_dir/filter.sed $OUT.new > $OUT
  rm -f $TMPFILE $OUT.new

  cmp -s $OUT $EXP
  status=$?

  if [ "$status" = 0 ] ; then
  	echo "$test_name: $test_description: ok"
  	touch $test_name.ok
  else
  	echo "$test_name: $test_description: failed"
  	diff $DIFF_OPTS $EXP $OUT > $test_name.failed
  fi

  unset OUT EXP
  ```

---

### Step 3: Synthesize a Deterministic, Minimal Filesystem Image

Follow the image construction techniques in `doc/developers/create-a-reproducer.md` while ensuring **100% deterministic output across environments**:

1. **Keep the image small and fast**:
   - Use a small block size (`-b 1024`) and block count (e.g., `100` to `1024` blocks) unless the bug specifically requires larger sizes or 4K blocks.
   - Always pass `-F -o Linux` (and explicit `-t ext4` / `-O <features>`) to `$MKE2FS`.
   - If the test takes over 60 seconds, create an empty `tests/<prefix>_<name>/is_slow_test` file; otherwise keep runtime under a second.

2. **Use `debugfs` to construct the exact on-disk state and corruption**:
   - **Files & directories**: `write /dev/null testfile`, `mkdir`, `mknod`, `symlink`, `link`, `unlink`, `kill_file`, `expand_dir`. (If non-empty file data is needed, slice from `$TEST_BITS`: `dd if=$TEST_BITS of=$TEST_DATA bs=4k count=1 > /dev/null 2>&1`, write `$TEST_DATA`, and `rm -f $TEST_DATA`.)
   - **Bitmaps**: `setb <blk> [count]`, `freeb <blk> [count]`, `seti <ino> [count]`, `freei <ino> [count]`.
   - **Block mappings & extents**:
     - `bmap <filespec> <lblk> <pblk>`, `fallocate <filespec> <start> <end>`, `punch <filespec> <start> <end>`.
     - `extent_open <filespec>` (`eo`), `insert_node [--after] [--uninit] <lblk> <len> <pblk>`, `replace_node [--uninit] <lblk> <len> <pblk>`, `delete_node`, `split_node` (`split`), `set_bmap [--uninit] <lblk> <pblk>`, `fix_parents` (`fixp`), `root_node` (`root`), `down_node` (`down`), `up_node` (`up`), `next_sib` (`ns`), `prev_sib` (`ps`), `next_leaf` (`nl`), `prev_leaf` (`pl`), `next` (`n`), `prev` (`p`), `goto_block <lblk>`, `extent_close` (`ec`).
   - **Inode fields** (`sif` / `set_inode_field`): `sif <filespec> <field> <value>` (e.g., `size`, `blocks`, `links_count`, `mode`, `flags`, `dtime`, `bmap[0]`, `block[IND]`, `file_acl`, `extra_isize`, `projid`, `checksum`).
   - **Superblock fields** (`ssv` / `set_super_value`): `ssv <field> <value>` (e.g., `last_orphan`, `free_blocks_count`, `free_inodes_count`, `state`, `feature_compat`, `feature_incompat`, `feature_ro_compat`, `desc_size`, `orphan_file_inum`).
   - **Block group descriptors** (`set_bg`): `set_bg <bg_num> <field> <value>` (e.g., `free_blocks_count`, `free_inodes_count`, `flags`, `itable_unused`, `bg_checksum calc`).
   - **Extended attributes, journals, and raw block zapping**:
     - `ea_set`, `ea_get`, `ea_list`, `ea_rm`
     - `journal_open` (`jo`), `journal_write` (`jw`), `journal_close` (`jc`), `journal_run` (`jr`), `logdump`
     - `zap_block [-f <filespec>] [-o <offset>] [-l <length>] [-p <pattern>] [-b <bit>] <blk>`

3. **Eliminate Non-Determinism in Logged Output**:
   - Pass all logged output through `sed -f $cmd_dir/filter.sed` (automatic in `run_e2fsck` and `run_mke2fs`), which strips tool version banners, UUIDs, timestamps, directory hash seeds, and `csum 0x...` values.
   - Never print the random `$TMPFILE` name into logged output without filtering: pass `-N test_filesys` to `$FSCK`, and echo `test.img` in any synthetic command headers written to `$OUT.new`.
   - If the test output depends on timestamps, UUIDs, or checksums that are not stripped by `filter.sed`, pin them explicitly:
     - Set `E2FSCK_TIME=1176238800; export E2FSCK_TIME` (and `unset E2FSCK_TIME` at the end of `script`).
     - In `debugfs`: `set_current_time 20070410210000`, `set_super_value lastcheck 0`, `set_super_value hash_seed null`, `set_super_value mkfs_time 0`.
     - Use portable POSIX `/bin/sh` syntax (e.g., `[ "$a" = "$b" ]` instead of `[ "$a" == "$b" ]`, and avoid non-standard `dd` or `sed` flags).

---

### Step 4: Generate Golden `expect` Files and Verify the Test

1. **Create the test files**:
   - Create `tests/<prefix>_<name>/name` and `tests/<prefix>_<name>/script`.
   - Create empty placeholder expected output files (`touch tests/<prefix>_<name>/expect.1 tests/<prefix>_<name>/expect.2` for `run_e2fsck` tests, or `touch tests/<prefix>_<name>/expect` for single-log tests).

2. **Run the test with `test_script` and inspect the generated logs**:
   - Ensure `test_script` is built and execute the test manually:
     ```sh
     make -C tests test_script
     (cd tests && ./test_script <prefix>_<name>)
     ```
   - Inspect `tests/<prefix>_<name>.1.log` and `tests/<prefix>_<name>.2.log` (or `tests/<prefix>_<name>.log` and `tests/<prefix>_<name>.failed`).
   - Verify that:
     - Pass 1 exercises the exact target code path / corruption check in `e2fsprogs`.
     - Pass 2 (`expect.2`) reports a clean filesystem with `Exit status is 0`.
     - If testing a bug fix, verify that running the test against the unpatched binary fails (or crashes/corrupts as expected) and succeeds with the fix.

3. **Save the expected output and re-verify**:
   - For two-pass `run_e2fsck` tests, either run:
     ```sh
     (cd tests && ./scripts/repair-test <prefix>_<name>)
     ```
     or copy `tests/<prefix>_<name>.1.log` to `tests/<prefix>_<name>/expect.1` and `tests/<prefix>_<name>.2.log` to `tests/<prefix>_<name>/expect.2`.
   - For single-output tests, copy `tests/<prefix>_<name>.log` to `tests/<prefix>_<name>/expect`.
   - Re-run `(cd tests && ./test_script <prefix>_<name>)` and confirm it prints `<prefix>_<name>: <description>: ok` and `1 tests succeeded	0 tests failed`.
   - Clean up any `.ok`, `.log`, or `.failed` artifacts in `tests/` if needed, and ensure `git status` shows only the intended files under `tests/<prefix>_<name>/`.
