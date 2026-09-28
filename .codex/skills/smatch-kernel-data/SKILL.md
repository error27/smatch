---
name: smatch-kernel-data
description: Edit, validate, and commit Linux-kernel project data in Smatch's smatch_data/kernel nested repository. Use for function tables, parameter annotations, ignore lists, and similar analyzer data changes after the behavior is justified.
---

# Smatch kernel data

Make the smallest source-proven change under `smatch_data/kernel/`.  That
directory is a separate Git repository referenced by the parent Smatch tree.
Do not use project data merely to hide an inconvenient warning.

## Select and verify the entry

Inspect the target file's comments, nearby entries, and the Smatch code which
loads it before editing.  Similar names need not have identical formats.
Common forms include:

- `ignore_uninitialized_param`: `<function> <zero-based-parameter>` for an
  output parameter deliberately treated as initialized despite ignored or
  unmodelled error returns.
- `check_zero_to_err_ptr.ignore`: one function or macro whose deliberate
  valid-pointer use must not trigger that check.
- Parameter tables such as `frees_argument`, `puts_argument`, `sizeof_param`,
  and `gfp_flags`: follow their documented column order and zero-based
  parameter numbering.
- Function-name sets such as `allocation_funcs`, `returns_err_ptr`, and
  `no_return_funcs`: add only source-proven classifications.

Generated files name their generator.  Do not regenerate one merely to add a
requested override; broad regeneration can replace unrelated work.  Preserve
ordering unless the file is explicitly a sorted set, and never reorder
unrelated entries.

Conditional facts do not belong in unconditional tables unless that table can
represent the condition.  Use warning-review reasoning first unless the user
has explicitly established the behavior.

## Preserve nested repository state

Before editing, inspect both repositories from the active Smatch tree:

```sh
git status --short
git -C smatch_data/kernel status --short
```

Assume pre-existing modifications and untracked files belong to the user.
Edit, stage, and commit only the requested data file.  Never clean the nested
repository just to make the parent gitlink appear clean.

## Validate the behavior

Test the affected warning or database fact, not just the entry's spelling.  Run
the focused `smatch_scripts/kchecker` command in the kernel tree which produced
the warning.  Preserve non-normalized source paths when Kbuild uses them to
form include flags.  Record exact before/after warning output; use `(no output)`
only when a command ran successfully and the new entry suppressed the warning.
If a test cannot run, report the actual failure rather than treating an empty
filtered pipeline as success.

For database classifications, query the affected function or rebuild the
smallest needed input.  Confirm the data is no broader than source behavior.

## Commit nested changes

Commit the selected file within `smatch_data/kernel` first, then stage only the
`smatch_data/kernel` gitlink in the parent and commit that revision.  Existing
uncommitted nested changes can leave the submodule marked modified afterward;
they do not belong in either commit.

Follow the repository's `AGENTS.md` for authorship, message wrapping, required
Before/After evidence, and trailers.  Do not add a `Signed-off-by` trailer
unless the user explicitly asks for one.
