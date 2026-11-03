---
name: create-a-regression-test
description: Create a new, self-contained e2fsprogs regression test under tests/<prefix>_<name>/ (or convert an existing reproducer into a test) and verify it with tests/test_script. Use when asked to add, write, or convert a regression test under tests/ for e2fsck, mke2fs, debugfs, tune2fs, resize2fs, dumpe2fs, e2image, e2undo, or libext2fs (for C or libss unit tests such as tst_* in lib/ext2fs/, use writing-unit-tests instead).
---

# Creating an `e2fsprogs` Regression Test

When creating or converting a regression test in `e2fsprogs`, read and follow the complete guide in [`doc/developers/create-a-regression-test.md`](../../../doc/developers/create-a-regression-test.md) (and [`doc/developers/create-a-reproducer.md`](../../../doc/developers/create-a-reproducer.md) for image synthesis techniques).

## Required Reading

Before writing any test files, read:

1. `doc/developers/create-a-regression-test.md` — Complete step-by-step instructions, test harness conventions, script templates, determinism rules, and verification steps.
2. `doc/developers/create-a-reproducer.md` — `mke2fs` and `debugfs` commands for synthesizing minimal ext4 filesystem images and on-disk corruptions from scratch.

## Workflow Summary

1. **Choose the test category prefix and directory (`tests/<prefix>_<name>/`)**:
   - `f_*` (`e2fsck` corruption/repair), `j_*` (journal), `d_*` (`debugfs`/`dumpe2fs`), `m_*` (`mke2fs`), `r_*` (`resize2fs`), `t_*` (`tune2fs`), `i_*` (`e2image`), `u_*` (`e2undo`), `e_*` (`libext2fs`).
   - Remember that `tests/<prefix>_<name>/script` is **sourced** by `test_one`: always use `return 0` (never `exit 0`) when skipping a test, and `unset` custom variables at the end of `script`.

2. **Select the appropriate test pattern** (see Step 2 of `doc/developers/create-a-regression-test.md`):
   - **Pattern A (`f_*`, `j_*`)**: Two-pass `e2fsck` test using `SKIP_GUNZIP="true"` and `. $cmd_dir/run_e2fsck` with `name`, `script`, `expect.1`, and `expect.2`.
   - **Pattern B (`m_*`)**: `mke2fs` test sourcing `. $cmd_dir/run_mke2fs` with `expect.1`.
   - **Pattern C (`d_*`, `t_*`, `r_*`, `i_*`, `u_*`, `j_*`)**: Custom command test logging to `$test_name.log`, filtering through `sed -f $cmd_dir/filter.sed`, and comparing against `expect`.

3. **Synthesize a minimal, deterministic image** (see Step 3 of `doc/developers/create-a-regression-test.md` and `doc/developers/create-a-reproducer.md`):
   - Prefer programmatic image generation (`$MKE2FS` + `$DEBUGFS` with `SKIP_GUNZIP="true"`) over binary `image.gz` files.
   - Keep images small (`-b 1024`, `100`–`1024` blocks) and always pass `-F -o Linux` to `$MKE2FS`.
   - Avoid leaking `$TMPFILE` names, unpinned timestamps, or other non-deterministic values into logged output.

4. **Generate `expect` files and verify** (see Step 4 of `doc/developers/create-a-regression-test.md`):
   - Build and run the test harness:
     ```sh
     make -C tests test_script
     (cd tests && ./test_script <prefix>_<name>)
     ```
   - Inspect the generated `.log` / `.failed` files to verify the target code path is exercised and that the test fails without the fix and passes with the fix.
   - Populate `expect.1` / `expect.2` (e.g., via `(cd tests && ./scripts/repair-test <prefix>_<name>)`) or `expect`, re-run `(cd tests && ./test_script <prefix>_<name>)`, and remove temporary `.ok`, `.log`, and `.failed` files from `tests/`.
