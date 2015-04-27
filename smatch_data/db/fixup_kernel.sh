#!/bin/bash

db_file=$1
cat << EOF | sqlite3 $db_file
/* we only care about the main ->read/write() functions. */
delete from caller_info where function = '(struct file_operations)->read' and file != 'fs/read_write.c';
delete from caller_info where function = '(struct file_operations)->write' and file != 'fs/read_write.c';
delete from function_ptr where function = '(struct file_operations)->read';
delete from function_ptr where function = '(struct file_operations)->write';

/* delete these function pointers which cause false positives */
delete from caller_info where function = '(struct file_operations)->open' and type != 0;
delete from caller_info where function = '(struct notifier_block)->notifier_call' and type != 0;
delete from caller_info where function = '(struct mISDNchannel)->send' and type != 0;
delete from caller_info where function = '(struct irq_router)->get' and type != 0;
delete from caller_info where function = '(struct irq_router)->set' and type != 0;
delete from caller_info where function = '(struct net_device_ops)->ndo_change_mtu' and caller = 'i40e_dbg_netdev_ops_write';
delete from caller_info where function = '(struct timer_list)->function' and type != 0;

/* type 1003 is USER_DATA */
delete from caller_info where caller = 'hid_input_report' and type = 1003;
delete from caller_info where caller = 'nes_process_iwarp_aeqe' and type = 1003;
delete from caller_info where caller = 'oz_process_ep0_urb' and type = 1003;
delete from caller_info where function = 'dev_hard_start_xmit' and key = '\$' and type = 1003;
delete from caller_info where function like '%->ndo_start_xmit' and key = '\$' and type = 1003;
delete from caller_info where caller = 'packet_rcv_fanout' and function = '(struct packet_type)->func' and parameter = 1 and type = 1003;
delete from caller_info where caller = 'hptiop_probe' and type = 1003;
delete from caller_info where caller = 'p9_fd_poll' and function = '(struct file_operations)->poll' and type = 1003;
delete from caller_info where caller = 'proc_reg_poll' and function = 'proc_reg_poll ptr poll' and type = 1003;
delete from caller_info where function = 'blkdev_ioctl' and type = 1003 and parameter = 0 and key = '\$';

insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 1003, 0, '\$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 1003, 1, '\$', '1');
insert into caller_info values ('userspace', '', 'compat_sys_ioctl', 0, 0, 1003, 2, '\$', '1');

delete from caller_info where function = '(struct timer_list)->function' and parameter = 0;

/*
 * rw_verify_area is a very central function for the kernel.  The 1000000000
 * isn't accurate but I've picked it so that we can add "pos + count" without
 * wrapping on 32 bits.
 */
delete from return_states where function = 'rw_verify_area';
insert into return_states values ('faked', 'rw_verify_area', 0, 1, '0-1000000000[<=\$3]', 0, 0,   -1,      '', '');
insert into return_states values ('faked', 'rw_verify_area', 0, 1, '0-1000000000[<=\$3]', 0, 102,  2, '*\$', '0-1000000000');
insert into return_states values ('faked', 'rw_verify_area', 0, 1, '0-1000000000[<=\$3]', 0, 1011, 3,  '\$', '0-1000000000');
insert into return_states values ('faked', 'rw_verify_area', 0, 2, '(-4095)-(-1)',     0, 0,   -1,      '', '');

/*
 * I am a bad person for doing this to __kmalloc() which is a very deep function
 * and can easily be removed instead of to kmalloc().  But kmalloc() is an
 * inline function so it ends up being recorded thousands of times in the
 * database.  Doing this is easier.
 *
 */
delete from return_states where function = '__kmalloc';
insert into return_states values ('faked', '__kmalloc', 0, 1, '16', 0,    0,  -1, '', '');
insert into return_states values ('faked', '__kmalloc', 0, 1, '16', 0, 1011,   0, '\$', '0');
insert into return_states values ('faked', '__kmalloc', 0, 2, '0,4096-ptr_max', 0,    0, -1, '', '');
insert into return_states values ('faked', '__kmalloc', 0, 2, '0,4096-ptr_max', 0, 1011,  0, '\$', '1-4000000');
insert into return_states values ('faked', '__kmalloc', 0, 3, '0', 0,    0,  -1, '', '');
insert into return_states values ('faked', '__kmalloc', 0, 3, '0', 0,    1011,  0, '\$', '4000000-long_max');

delete from return_states where function = 'vmalloc';
insert into return_states values ('faked', 'vmalloc', 0, 1, '0,4096-ptr_max', 0,    0, -1, '', '');
insert into return_states values ('faked', 'vmalloc', 0, 1, '0,4096-ptr_max', 0, 1011,  0, '\$', '1-128000000');
insert into return_states values ('faked', 'vmalloc', 0, 2, '0', 0,    0,  -1, '', '');

delete from return_states where function = 'ksize';
insert into return_states values ('faked', 'ksize', 0, 1, '0', 0,    0, -1, '', '');
insert into return_states values ('faked', 'ksize', 0, 1, '0', 0, 1011,  0, '\$', '16');
insert into return_states values ('faked', 'ksize', 0, 2, '1-4000000', 0,    0,  -1, '', '');

/* store a bunch of capped functions */
update return_states set return = '0-u32max[<=\$2]' where function = 'copy_to_user';
update return_states set return = '0-u32max[<=\$2]' where function = '_copy_to_user';
update return_states set return = '0-u32max[<=\$2]' where function = '__copy_to_user';
update return_states set return = '0-u32max[<=\$2]' where function = 'copy_from_user';
update return_states set return = '0-u32max[<=\$2]' where function = '_copy_from_user';
update return_states set return = '0-u32max[<=\$2]' where function = '__copy_from_user';

/* 64 CPUs aught to be enough for anyone */
update return_states set return = '1-64' where function = 'cpumask_weight';

update return_states set return = '0-8' where function = '__arch_hweight8';
update return_states set return = '0-16' where function = '__arch_hweight16';
update return_states set return = '0-32' where function = '__arch_hweight32';
update return_states set return = '0-64' where function = '__arch_hweight64';

/*
 * Preserve the value across byte swapping.  By the time we use it for math it
 * will be byte swapped back to CPU endian.
 */
update return_states set return = '0-u64max[==\$0]' where function = '__fswab64';
update return_states set return = '0-u32max[==\$0]' where function = '__fswab32';
update return_states set return = '0-u16max[==\$0]' where function = '__fswab16';

delete from return_states where function = 'bitmap_allocate_region' and return = '1';
delete from return_states where function = 'pci_bus_read_config_word' and return = 135;
delete from return_states where function = 'pci_bus_write_config_word' and return = 135;

update return_states set return = '(-4095)-s32max[<=\$3]' where function = 'get_user_pages' and return = 's32min-s32max';
update return_states set return = '(-4095)-s64max[<=\$3]' where function = 'get_user_pages' and return = 's64min-s64max';

/* Smatch can't parse wait_for_completion() */
update return_states set return = '(-108),(-22),0' where function = '__spi_sync' and return = '(-115),(-108),(-22)';

delete from caller_info where caller = '__kernel_write';

EOF

call_id=$(echo "select distinct call_id from caller_info where function = '__kernel_write';" | sqlite3 $db_file)
for id in $call_id ; do
    echo "insert into caller_info values ('fake', '', '__kernel_write', $id, 0, 1, 1003, '*\$', '0-1000000000');" | sqlite3 $db_file
done

for i in $(echo "select distinct return from return_states where function = 'clear_user';" | sqlite3 $db_file ) ; do
    echo "update return_states set return = \"$i[<=\$1]\" where return = \"$i\" and function = 'clear_user';" | sqlite3 $db_file
done


