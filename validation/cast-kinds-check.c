#include "linear/cast-kinds.c"

/*
 * check-name: cast-kinds check
 * check-command: sparse -m64 -v $file
 *
 * check-error-start
linear/cast-kinds.c:5:45: warning: cast drops bits
linear/cast-kinds.c:6:47: warning: cast drops bits
linear/cast-kinds.c:7:46: warning: cast drops bits
linear/cast-kinds.c:8:45: warning: cast drops bits
linear/cast-kinds.c:12:48: warning: cast drops bits
linear/cast-kinds.c:13:50: warning: cast drops bits
linear/cast-kinds.c:14:49: warning: cast drops bits
linear/cast-kinds.c:15:48: warning: cast drops bits
linear/cast-kinds.c:21:49: warning: cast wasn't removed
linear/cast-kinds.c:22:48: warning: cast wasn't removed
linear/cast-kinds.c:28:52: warning: cast wasn't removed
linear/cast-kinds.c:29:51: warning: cast wasn't removed
linear/cast-kinds.c:34:52: warning: cast wasn't removed
linear/cast-kinds.c:35:54: warning: cast wasn't removed
linear/cast-kinds.c:36:52: warning: cast wasn't removed
 * check-error-end
 */
