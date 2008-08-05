/*

1)  Name every path
    Child paths?
2)  Save the path number in the state history
3)  Possible value ranges
4)  Binary, int, equal to another variable
5)  When can we use this data?

*/

static int my_id;

void register_smatch_extra(int id)
{
	my_id = id;
}
