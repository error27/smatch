---
name: smatch-create-reproducer
description: Create and minimize a reproducer for a Smatch analyzer bug first observed while checking Linux kernel code. Use when source-level simplification does not reproduce the faulty state and the exact preprocessed kernel translation unit must be reduced.
---

# Smatch reproducer

Create a reproducer which demonstrates the analyzer defect itself.  Its expected
validation output must describe ideal Smatch behavior: the test fails while the
bug exists and passes after the fix.

## Establish the oracle

Instrument the first point where state becomes wrong, not merely a later
warning.  Include `check_debug.h` and bracket the probe with
`__smatch_force_on()` and `__smatch_force_off()` when normal output controls
could suppress it.  For an unexpectedly empty range:

```c
{
	__smatch_force_on();
	__smatch_implied(size);
	__smatch_force_off();
}
```

Choose one stable output substring, such as `size = ''`, and confirm it appears
in the original kernel analysis before reducing anything.

## Preserve the kernel parse

When hand-written C does not reproduce the defect, reduce the preprocessed
translation unit:

1. Add the debug probe to the affected kernel source or header.
2. Generate the matching `.i` file using the same architecture, cross compiler,
   configuration, and build tree as the failed analysis.
3. Run the kernel build with `V=1` and capture the complete Smatch invocation.
   Do not recreate include paths and compiler definitions by hand.
4. Replay that command with its final source argument replaced by the generated
   `.i` file.  Keep working directory and database selection unchanged.
5. Test the exact oracle with a fixed-string search such as
   `grep -F "size = ''"`.

An exact replay can fail before analysis because `-include`, `-I`, `-D`, and
`-Wp,-MMD` preprocess the `.i` a second time.  Record that result, then retry
after removing only preprocessing options.  Retain project, architecture,
endianness, word size, database, and semantic compiler flags.  Do not confuse
header redefinition diagnostics with loss of the analyzer bug.

Do not begin reduction until the `.i` command reproduces the same bad state.
If preprocessing loses the defect, report that fact and retain the command and
probe location for later debugging.

## Reproduce database-dependent defects

When the stack enters a cross-function database callback, a source file alone
is not a complete reproducer:

1. Run the preprocessed file with `--info` and save its output.
2. Create a small database from that output with
   `smatch_data/db/create_db.sh`, using the same project.
3. Confirm the relevant function is in `return_states`; preserve related
   `caller_info` when the failure is call-site dependent.  Check that stored
   `PARAM_LIMIT` values fit their declared parameter types.  Duplicate rows
   with differently signed ranges may reveal serialization before casting.
4. Run the second pass with an absolute `--db-file=` path.  C-Reduce runs tests
   in a temporary directory, so a relative database path selects another DB.

Static summaries use source-file identity.  Retain the original basename in
both database-building and analysis passes.  A compact validation can need
different first- and second-pass sources: define a static callee to build the
DB, then declare it for analysis when an inline definition would bypass its
stored summary.

Bracket a suspect call with `__smatch_local_debug_on()` and
`__smatch_local_debug_off()` to see impossible selected return states.  A lack
of CULL does not rule out a bad DB range; inspect stored `PARAM_LIMIT` values
separately.

## Reduce and validate

Work on a copy of the `.i` file.  The C-Reduce interestingness test must run
the captured Smatch command, redirect diagnostics consistently, fixed-string
match the oracle, and exit zero only while the defect remains.  Invoke it by
hand repeatedly before `creduce`.  Reject parser-error candidates and require
textual landmarks for the call and constraints when malformed code can emit the
same debug output.

Move the minimized source into `validation/` or `validation/kernel/` as
appropriate.  Set `check-output-start` to the ideal output, not buggy output;
run the single test to show it fails only for the intended reason.  Preserve
unrelated worktree changes and remove temporary kernel probes when done.
