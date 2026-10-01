#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

struct sm_terminate_elem_id {
	char name[16];
};

extern bool elem_id_matches(const struct sm_terminate_elem_id *id);

int sm_terminate_elem_id_matches(void __user *src)
{
	struct sm_terminate_elem_id id;
	char bad[16];

	copy_from_user(id.name, src, sizeof(id.name));
	if (!elem_id_matches(&id))
		return -1;
	strlen(id.name);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: elem_id_matches NUL terminates name
 * check-command: validation/kernel/build.sh sm_terminate12.c
 *
 * check-output-start
sm_terminate12.c:23 sm_terminate_elem_id_matches() warn: unterminated user string: 'bad'
 * check-output-end
 */
