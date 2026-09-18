int choose(int value)
{
	if (value)
		return 1;
	return 0;
}

/*
 * check-name: smatch: delete database entry
 * check-command: validation/smatch_del_entry_test.sh sm_del_db_entry1.c
 *
 * check-output-start
0|1013|0|$|one
1|103|0|$|keep
 * check-output-end
 */
