# Cross-function and range review

Treat database output as analyzer evidence.  Verify each return-state row
against callee source.  In a relationship such as `range[==$2]`, the equality
claim and numeric range are separate facts; discard an equality that source
disproves while retaining independently valid range information.

For an unexpected value, trace that exact value backward through assignments,
`return_states`, thin wrappers, and indirect-call implementations.  Separate
the function propagating it from the deepest summary which introduces it.  If a
stored combined range includes the value but the direct callee's rows do not,
identify the first inconsistent or stale summary boundary instead of blaming
the direct callee.

When a callback is selected through a function-pointer member, trace the
containing operations table and the device/generation choice.  Do not merge all
implementations ever assigned to a same-named member.  Parameter-value records
for discriminator fields can also prove a callback branch unreachable.

For a small same-file callee which loses precision, bracket the call with
`__smatch_debug_db_on()` and `__smatch_debug_db_off()` and inspect the in-memory
rows and state dump.  Map numeric record types through `smatch_dbtypes.h`.
Find the first inner return merge where an unchanged sibling state disappears;
a later dereference usually only exposes that lost state.

Parameter-limit/set/clear records do not encode bit-level implications.  If a
callee returns NULL only with a specific flag set, check whether that unchanged
flag leads the caller to a non-dereferencing branch.  Do not infer a bit from a
broad numeric range alone.

Use a narrow return-range override only after source proves an impossible row
persists through a fresh database rebuild.  Match the exact faulty row and
preserve valid NULL, pointer, and `ERR_PTR()` rows.  Do not use an override to
paper over stale data or an unreviewed source behavior.

For a valid pointer passed to `PTR_ERR()`, a preceding `IS_ERR()` may prove it
safe even when it repeats a side-effect-free accessor call.  Verify equal
arguments, no side effects, and no intervening mutation before relying on this
pattern.

For mixed positive and negative return values, inspect the protocol/class
branch that produces positives.  A generic helper's positive outcome may be
unreachable for the caller's request type.  When the interface intentionally
mixes outcomes, document the return convention close to the static function.
If a path has already converted all nonzero statuses to errors, return literal
`0` on its success path rather than returning the status expression again.

For uninitialized output parameters, inspect all call sites.  If callers
consistently and deliberately ignore a result and consume the output as always
available, use the narrow kernel data entry
`<function> <zero-based-parameter>` in `smatch_data/kernel/ignore_uninitialized_param`.
Do not suppress a genuinely unsafe output merely because one caller omitted
error handling.

For an uninitialized scalar passed to a helper, a guard inside the helper does
not ordinarily make the call safe: evaluating the argument is undefined in C
even if the helper would return before using its parameter.  The kernel may
accept this pattern only when the helper is declared `inline` and the guard
proves that the inlined body never reads the value on that path.  A plain
`inline` declaration is sufficient for this review convention, although
`__always_inline` would make that reliance stronger.  Do not apply the
exception to a non-inline or out-of-line helper.
