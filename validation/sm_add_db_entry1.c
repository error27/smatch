int choose(int value)
{
	if (value)
		return 1;
	return 0;
}

/*
 * check-name: smatch: add database entry
 * check-command: validation/smatch_add_entry_test.sh sm_add_db_entry1.c
 *
 * check-output-start
1|103|0|$|1-10|1
 * check-output-end
 */
