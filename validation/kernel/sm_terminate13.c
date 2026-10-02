#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "../../check_debug.h"

static char *sm_terminate_next_string(struct sk_buff *skb)
{
	char *string = skb->data;
	int i;

	for (i = 0; i < skb->len; i++) {
		if (string[i] != '\n')
			continue;
		string[i] = '\0';
		skb_pull(skb, i + 1);
		return string;
	}

	return NULL;
}

int sm_terminate_skb_string(struct sk_buff *skb, void __user *src)
{
	char *string;
	char bad[16];

	string = sm_terminate_next_string(skb);
	if (!string)
		return -1;
	strlen(string);

	copy_from_user(bad, src, sizeof(bad));
	strlen(bad);

	return 0;
}

/*
 * check-name: smatch: terminated skb string return
 * check-command: validation/kernel/build.sh sm_terminate13.c
 *
 * check-output-start
sm_terminate13.c:33 sm_terminate_skb_string() warn: unterminated user string: 'bad'
 * check-output-end
 */
