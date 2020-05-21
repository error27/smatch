#define __visible __attribute__((externally_visible))

__visible void foo(void)
{
}

int flag __visible;

/*
 * check-name: attr-visible
 * check-command: sparse -Wdecl $file
 */
