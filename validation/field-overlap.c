struct {
	int x;
	struct {
		int z;
		int w;
	} y;
} a = { .y.z = 1, .y.w = 2, };

struct {int x, y, z;} w[2] = {
	{.x = 1, .y = 2, .z = 3},
	{.x = 1, .y = 2, .z = 3}
};
