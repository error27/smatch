int add_yx_y(int p, int x, int y) { return (p ? (y+x) : y) == ((p ? x : 0) + y); }
int add_xy_y(int p, int y, int x) { return (p ? (x+y) : y) == ((p ? x : 0) + y); }
int add_xy_x(int p, int x, int y) { return (p ? (x+y) : x) == ((p ? y : 0) + x); }
int add_yx_x(int p, int y, int x) { return (p ? (y+x) : x) == ((p ? y : 0) + x); }
int add_y_yx(int p, int x, int y) { return (p ? y : (y+x)) == ((p ? 0 : x) + y); }

int ior_yx_y(int p, int x, int y) { return (p ? (y|x) : y) == ((p ? x : 0) | y); }
int ior_xy_y(int p, int y, int x) { return (p ? (x|y) : y) == ((p ? x : 0) | y); }
int ior_xy_x(int p, int x, int y) { return (p ? (x|y) : x) == ((p ? y : 0) | x); }
int ior_yx_x(int p, int y, int x) { return (p ? (y|x) : x) == ((p ? y : 0) | x); }
int ior_y_yx(int p, int x, int y) { return (p ? y : (y|x)) == ((p ? 0 : x) | y); }

int xor_yx_y(int p, int x, int y) { return (p ? (y^x) : y) == ((p ? x : 0) ^ y); }
int xor_xy_y(int p, int y, int x) { return (p ? (x^y) : y) == ((p ? x : 0) ^ y); }
int xor_xy_x(int p, int x, int y) { return (p ? (x^y) : x) == ((p ? y : 0) ^ x); }
int xor_yx_x(int p, int y, int x) { return (p ? (y^x) : x) == ((p ? y : 0) ^ x); }
int xor_y_yx(int p, int x, int y) { return (p ? y : (y^x)) == ((p ? 0 : x) ^ y); }

/*
 * check-name: fact-select01
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
