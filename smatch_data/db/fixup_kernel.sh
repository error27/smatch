#!/bin/bash

# mark some paramaters as coming from user space
cat << EOF | sqlite3 smatch_db.sqlite
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 0, '$$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 1, '$$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 2, '$$', '1');

delete from caller_info where function = '(struct file_operations)->read' and file != 'fs/read_write.c';
delete from caller_info where function = '(struct file_operations)->write' and file != 'fs/read_write.c';

delete from caller_info where function = '(struct notifier_block)->notifier_call' and type != 0;
delete from caller_info where function = '(struct mISDNchannel)->send' and type != 0;

delete from caller_info where caller = 'hid_input_report' and type = 3;
delete from caller_info where caller = 'nes_process_iwarp_aeqe' and type = 3;
delete from caller_info where caller = 'oz_process_ep0_urb' and type = 3;
delete from caller_info where function = 'dev_hard_start_xmit' and key = '\$\$' and type = 3;
delete from caller_info where function like '%->ndo_start_xmit' and key = '\$\$' and type = 3;
delete from caller_info where caller = 'packet_rcv_fanout' and function = '(struct packet_type)->func' and parameter = 1 and type = 3;

delete from caller_info where caller = 'hptiop_probe' and type = 3;

delete from caller_info where function = '(struct timer_list)->function' and parameter = 0;

delete from return_states where function = 'rw_verify_area';
insert into return_states values ('faked', 'rw_verify_area', 0, 1, '0-1000000', 0, 0, -1, '', '');
insert into return_states values ('faked', 'rw_verify_area', 0, 1, '0-1000000', 0, 11, 2, '*\$\$', '0-1000000');
insert into return_states values ('faked', 'rw_verify_area', 0, 1, '0-1000000', 0, 11, 3, '\$\$', '0-1000000');
insert into return_states values ('faked', 'rw_verify_area', 0, 2, '(-4095)-(-1)', 0, 0, -1, '', '');

update return_states set return = '0-u32max[<=p2]' where function = 'copy_to_user';
update return_states set return = '0-u32max[<=p2]' where function = '_copy_to_user';
update return_states set return = '0-u32max[<=p2]' where function = '__copy_to_user';
update return_states set return = '0-u32max[<=p2]' where function = 'copy_from_user';
update return_states set return = '0-u32max[<=p2]' where function = '_copy_from_user';
update return_states set return = '0-u32max[<=p2]' where function = '__copy_from_user';

EOF

call_id=$(echo "select distinct call_id from caller_info where function = '__kernel_write';" | sqlite3 smatch_db.sqlite)
for id in $call_id ; do
    echo "insert into caller_info values ('fake', '', '__kernel_write', $id, 0, 1, 3, '*\$\$', '0-1000000');" | sqlite3 smatch_db.sqlite
done

for i in $(echo "select distinct return from return_states where function = 'clear_user';" | sqlite3 smatch_db.sqlite ) ; do
    echo "update return_states set return = \"$i[<=p1]\" where return = \"$i\" and function = 'clear_user';" | sqlite3 smatch_db.sqlite
done


