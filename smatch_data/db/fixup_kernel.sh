#!/bin/bash

# mark some paramaters as coming from user space
cat << EOF | sqlite3 smatch_db.sqlite
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 0, '$$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 1, '$$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 3, 2, '$$', '1');

delete from caller_info where function = '(struct file_operations)->read' and file != 'fs/read_write.c';
delete from caller_info where function = '(struct file_operations)->write' and file != 'fs/read_write.c';

delete from caller_info where function = '(struct notifier_block)->notifier_call';

EOF

