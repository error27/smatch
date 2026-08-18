int test(int value)
{
	if (value)
		return 1;
	return 0;
}

/*
 * check-name: smatch: return states line
 * check-command: smatch --no-data --info sm_return_states_line.c | grep " 0, 0, -1"
 *
 * check-output-start
sm_return_states_line.c:4 test() SQL: insert into return_states values(0x16767feda71def09, 'test', 686547608178421760, 4, 1, '1', 0, 0, -1, '4', 'int(*)(int)');
sm_return_states_line.c:5 test() SQL: insert into return_states values(0x16767feda71def09, 'test', 686547608178421760, 5, 2, '0', 0, 0, -1, '5', 'int(*)(int)');
 * check-output-end
 */
