#include <linux/skbuff.h>
#include <linux/string.h>
#include "../../check_debug.h"

int sm_terminate_skb_data(struct sk_buff *skb)
{
	char *buf = (char *)skb->data;

	strlen(buf);

	if (!skb->len)
		return 0;
	skb->data[skb->len - 1] = '\0';
	strlen((char *)skb->data);

	return 0;
}

/*
 * check-name: smatch: unterminated skb->data string
 * check-command: validation/kernel/build.sh sm_terminate3.c
 *
 * check-output-start
sm_terminate3.c:9 sm_terminate_skb_data() warn: unterminated user string: 'buf'
 * check-output-end
 */
