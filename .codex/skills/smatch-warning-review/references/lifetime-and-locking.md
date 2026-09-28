# Lifetime, NULL, and locking review

For a freed-memory report, map every freeing path to its control-flow result.
A callee which frees an object and returns a non-continuation status may make a
later dereference unreachable.  Conversely, do not stop at a non-NULL proof:
teardown can free a containing object while a member pointer remains non-NULL.

In remove paths, successful probe can prove that a field is initialized, making
a later NULL check redundant.  Audit every operation between the first access
and that check for `device_del()`/`put_device()`, unregister, uninit, or a
release callback which can free the containing allocation.  Move field cleanup
before the final unregister operation.  Call a late contradictory NULL check a
code-quality issue separately from any discovered use-after-free.

For `dev_coredumpm()` reports, inspect the supplied free callback and its
database state.  A whole-parameter `FREED` record means the cookie is considered
freed; member refcount records describe only the member.  The callback may
release related state without freeing the data cookie.  Keep the independent
asynchronous lifetime requirement in view: objects used by coredump callbacks
must survive until the framework is finished with them.

For a pointer dereferenced before a NULL check, resolve its declared type.  An
embedded array is never a NULL pointer; zeroing its elements does not invalidate
the array storage.  Inspect the exact member a helper dereferences.

For an inverted-NULL-check warning, establish what the tested pointer means.
An optional request/output pointer may deliberately enable a block that
preloads `-ENOMEM` for shared cleanup after later allocation failures.  Verify
the actual allocation checks and every cleanup path before calling it a typo.

For inconsistent-lock-return warnings, retrieve the continuation lines from
the warning file: its `Locked on` and `Unlocked on` paths identify what to
compare.  Inspect an apparently unlocking helper's implementation and all
returns, callbacks, and retry loops; names, saved IRQ flags, and annotations
are corroboration, not proof.  Query cross-function lock records when needed.
Add a narrow manual locking model only when source proves the helper always
returns with the parameter-derived lock released and the model can express all
paths and IRQ restoration accurately.
