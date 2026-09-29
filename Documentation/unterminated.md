# Detecting unterminated user strings

This check looks for character buffers whose contents come from userspace and
which may not contain a NUL byte before their end.  Such a buffer is safe for
interfaces that take an explicit length, but passing it to an interface that
scans for the first NUL byte can read beyond the buffer.  The check should
warn only when both facts are known: the buffer is user controlled and
unterminated, and the callee requires a NUL-terminated string.

## Sources of user-controlled, unterminated strings

Initially, treat these as sources of bytes that may be a string but are not
guaranteed to be NUL terminated:

* `copy_from_user()` and `__copy_from_user()` copying into a character buffer.
* `memdup_user()` (and equivalent `memdup()` uses where the copied input is
  user controlled).
* `skb->data`, whose packet payload is controlled by the sender.

The source state should follow assignments and relevant structure members, and
must not imply that every user-controlled buffer is a C string.  A caller that
supplies an explicit length remains outside this check's scope.

## Establishing NUL termination

A buffer becomes known to be NUL terminated when the analysis can prove that
one of its bytes is zero within its bounds.  Important initial cases are:

* Zero-initialize the buffer, then copy fewer bytes than its full size.  The
  first byte not overwritten remains `\0`.
* Explicitly reserve and write a terminator, for example
  `buf[len - 1] = '\0';`, where `len` is a valid in-bounds buffer length.

Future refinements may recognize bounded string-copy helpers and successful
length checks, but they must preserve the distinction between an actual proof
of termination and a mere expectation that input is text.

## Functions that require a NUL-terminated string

The initial sink list should include functions that scan a string to find its
end or interpret an argument as a `%s`-style string.  Examples include:

* `strlen()`, `strnlen()` when its bound can exceed the buffer, and `strdup()`.
* `strcmp()`, `strcasecmp()`, `strcpy()`, `strcat()`, `strstr()`, and similar
  unbounded string helpers.
* `kstrtoint()`, `kstrtoul()`, and related string-to-number conversion helpers.
* `printk()`, `pr_info()`, `dev_info()`, `snprintf()`, and `sprintf()` when the
  relevant format conversion consumes an argument as `%s`.

This list is intentionally conservative.  Functions with an explicit maximum
length, such as `memcmp()` and `strncasecmp()`, do not require a terminating
NUL merely by being passed a character buffer.
