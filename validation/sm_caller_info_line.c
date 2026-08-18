void callee(int value);

void test(void)
{
	callee(1);
}

/*
 * check-name: smatch: caller info line
 * check-command: smatch --no-data --info sm_caller_info_line.c | grep '%call_marker%'
 *
 * check-output-start
sm_caller_info_line.c:5 test() SQL_caller_info: insert into caller_info values (0x5373e41d3ddb4c74, 'test', 'callee', %CALL_ID%, 5, 0, 0, -1, '%call_marker%', 'void(*)(int)');
 * check-output-end
 */
