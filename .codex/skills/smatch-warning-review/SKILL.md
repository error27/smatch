---
name: smatch-warning-review
description: Analyze a Smatch warning in Linux kernel code and classify it as a bug, code-quality issue, false positive, or unresolved.
---

# Smatch warning review

Review the warning against the exact kernel source and Smatch database that
produced it.  Lead with a clear verdict, then give the minimum concrete source
and range/lifetime evidence needed to support it.

## Review workflow

1. Read the reported source and enough surrounding control flow to identify the
   exact operation, object, and result that Smatch flagged.
2. Trace the value or object backwards through assignments, wrappers, and
   callers.  Trace forward to its first meaningful use when that determines
   whether a fault is reachable.
3. Prove reachable bounds, ownership, or lock state from source.  Treat
   cross-function database rows as evidence to verify, never as ground truth.
4. Classify the result:
   - **Runtime bug** — a reachable kernel path violates memory safety,
     arithmetic correctness, or a required API contract.
   - **Code-quality issue** — the source is misleading or inconsistent, but a
     runtime fault is not reachable.
   - **False positive** — a concrete invariant or validator makes the warned
     state unreachable, and explain why Smatch missed it.
   - **Unresolved** — the available evidence cannot establish one of the
     above.  State the precise missing fact.
5. Recommend a source fix only for a real issue.  Do not suggest redundant
   checks merely to silence a false positive unless requested.

For a false positive, name the dominating condition, derive the resulting
numeric range or state, and identify the lost relation (alias, callback target,
cross-function summary, bit condition, or stale database fact).

## Choose the relevant reference

Read the matching reference before drawing a conclusion:

- [`references/arithmetic-and-taint.md`](references/arithmetic-and-taint.md)
  for user-controlled lengths, allocation/copy sizes, integer overflows,
  pointer arithmetic, and array bounds.
- [`references/cross-function-and-ranges.md`](references/cross-function-and-ranges.md)
  for error ranges, return values, function pointers, callback selection,
  uninitialized output parameters, and database provenance.
- [`references/lifetime-and-locking.md`](references/lifetime-and-locking.md)
  for freed-memory, NULL, removal-path, refcount, coredump, and lock warnings.

## General evidence rules

Parsing input is not necessarily maximum-size validation.  Compare the exact
destination capacity at the pointer expression, including offsets, unions, and
protocol headers, against a source-proven input maximum.

Find validators which dominate the warned operation and check every relevant
branch.  A framework may constrain a callback parameter before dispatch; for
example, a file `.write` callback is reached from `vfs_write()` with `count`
capped at `MAX_RW_COUNT`.  Do not infer a bound from a convention or a comment
without inspecting the check and its failure path.

When Smatch is run with `--ai`, note the smallest missing fact that would have
avoided irrelevant traversal or instrumentation.  Suggest compact structured
warning output—per-operand ranges and short provenance—after reaching the
verdict; do not defer the verdict to redesign diagnostics.

## Database use

Use the project scripts and database belonging to the analyzed kernel tree.
Compare `return_states`, parameter records, and function-pointer records with
the current source.  A source-level return can have multiple inferred rows, but
each stored relationship must be independently true.  Rebuild or make a fresh
temporary database when a row appears stale before changing kernel code or
adding a Smatch data override.

For a suspected analyzer defect, preserve the source line, relevant SQL rows,
and the first call edge where state becomes imprecise.  Prefer a narrow,
source-proven data fix over suppressing all outcomes for a function.

## Scope

Do not expand a user-triggerability review into hypothetical misuse by trusted
in-kernel callers once the normal untrusted entry path has been proven safe.
Conversely, do not call an overflow harmless solely because allocation would
likely fail: establish whether the arithmetic operation itself is reachable and
whether its result is used.
