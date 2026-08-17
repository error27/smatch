int copy_from_user(void *to, const void *from, unsigned long size);

struct vm_area {
	unsigned long vm_start;
};

int test(struct vm_area *vma, const void *user)
{
	unsigned long addr;

	copy_from_user(&addr, user, sizeof(addr));
	if (addr < 4096)
		return -2;
	if (addr < vma->vm_start)
		return -1;
	return 0;
}

/*
 * check-name: smatch: check arm64 tagged addresses
 * check-command: validation/smatch_arch_test.sh arm64 --no-data --spammy -p=kernel sm_check_arm64_tagged.c
 *
 * check-output-start
sm_check_arm64_tagged.c:14 test() warn: comparison of a potentially tagged address (test, -2, addr)
 * check-output-end
 */
