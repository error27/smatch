#define __visible __attribute__((externally_visible))

int flag __visible;
int arr[2] __visible;

/*
 * check-name: attr-visible-after
 * check-command: sparse -Wdecl $file
 */
