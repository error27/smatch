# Arithmetic, taint, and bounds review

For an unchecked user length, trace it from producer to every copy,
allocation, command-size calculation, and firmware transfer.  Compute the
capacity at the actual destination expression: `object.data + 4` has
`sizeof(object.data) - 4` bytes left.  Include union layout and preceding
format fields.  A parsed element fitting in its source buffer does not prove it
fits a driver-private destination.

Before declaring a bug, look for a validator that dominates the sink.  Inspect
both branches and wrappers until the real bound is visible.  Examples of useful
framework invariants include cipher-specific key-length checks made by
`cfg80211_validate_key_settings()`, nonzero `NLA_BINARY` policy `.len` limits,
CAN frame validation, and the `vfs_write()` `MAX_RW_COUNT` cap.  Confirm the
validated attribute or input type is the one that reaches the sink.

Aliasing can hide such a relation: networking code may validate a typed object
through one cast of `skb->data` and consume it through another.  Prove the
common bytes and the dominating check before calling this a false positive.

For overflow warnings, identify the exact operator.  Apply C's usual arithmetic
conversions at that operator and distinguish safe multiplication from a later
unsafe addition.  For a user-controlled `size_t` warning, determine which
operands are actually user controlled; `__smatch_user_rl()` can separate a
full-range non-user value from real taint.  If user input can overflow the
operator, classify it as a bug even if a later check prevents a harmful use.

Do not let a later endpoint-wrap test prove an earlier shift safe.  With
`npages << PAGE_SHIFT`, high bits can be lost while the shifted size remains
small and nonzero.  Look for an explicit bound such as
`npages <= ULONG_MAX >> PAGE_SHIFT`, then determine whether inconsistent use of
the original page count creates an interface or memory-safety problem.

Check prior helpers receiving the same operands.  A helper which performs the
complete widened size calculation and rejects an unrepresentable request can
establish the invariant for a later expression, provided it covers all of that
expression's operands and the caller proceeds only after success.

For unsigned subtraction, evaluate the underflow case.  `a - b > 0` is
effectively `a != b` for unsigned operands, so it accepts `a < b` after wrap.
It is safe only if `b <= a` is a maintained invariant through initialization,
updates, loops, and callees.  Prefer comparisons before subtraction where the
invariant is not guaranteed.

For array off-by-one reports, distinguish forming `&array[size]` (valid) from
dereferencing it.  A coupled zero length may make the call safe only if the
callee returns before access.  Likewise, `test_bit(index, bitmap)` is not a
bounds check: the bitmap index must be in range before that access.

Follow a wrapped user-pointer calculation to its consumer.  A final
`copy_from_user()` still validates the resulting address; when the same caller
could supply that address directly and no derived pointer escapes, wrapping may
be harmless.  This reasoning does not apply to DMA, mapping/pinning ranges,
cross-process access, unchecked dereferences, or validation tied to the
original contiguous range.

For signed indexes and bit operations, prove the concrete set from callers,
static match tables, registration, and discriminator branches.  An enum's
negative sentinel or a broad signed type explains Smatch's range but does not
show reachability.  Preserve the warning if an unchecked sentinel can reach
`BIT()` or `set_bit()`.

An `ALIGN()` or round-up can be evaluated before validation without a runtime
bug only if every bad input is rejected and the wrapped result is not used,
compared, or passed anywhere before the dominating validator.  This cannot
excuse signed overflow.
