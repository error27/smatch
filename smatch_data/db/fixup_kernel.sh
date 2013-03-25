#!/bin/bash

# mark some paramaters as coming from user space
cat << EOF | sqlite3 smatch_db.sqlite
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 0, '$$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 1, '$$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 2, '$$', '1');

delete from caller_info where function = '(struct file_operations)->read' and file != 'fs/read_write.c';
delete from caller_info where function = '(struct file_operations)->write' and file != 'fs/read_write.c';

delete from caller_info where function = '(struct notifier_block)->notifier_call';
delete from caller_info where caller = 'hid_input_report' and type = 3;
delete from caller_info where caller = 'nes_process_iwarp_aeqe' and type = 3;
delete from caller_info where caller = 'oz_process_ep0_urb' and type = 3;
delete from caller_info where function = 'dev_hard_start_xmit' and key = '\$\$' and type = 3;
delete from caller_info where function like '%->ndo_start_xmit' and key = '\$\$' and type = 3;
delete from caller_info where caller = 'packet_rcv_fanout' and function = '(struct packet_type)->func' and parameter = 1 and type = 3;

delete from caller_info where caller = 'hptiop_probe' and type = 3;

EOF

