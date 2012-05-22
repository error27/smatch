#!/bin/bash

# mark some paramaters as coming from user space
cat << EOF | sqlite3 smatch_db.sqlite
insert into caller_info values ('userspace', 'compat_sys_ioctl', 0, 0, 3, 0, '$$', '1');
insert into caller_info values ('userspace', 'compat_sys_ioctl', 0, 0, 3, 1, '$$', '1');
insert into caller_info values ('userspace', 'compat_sys_ioctl', 0, 0, 3, 2, '$$', '1');
EOF

