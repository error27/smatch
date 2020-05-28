void funf(float);
void fund(double);
void funl(long double);

#define fung(X) _Generic(X,		\
	float:		funf,		\
	default:	fund,		\
	long double:	funl) (X)

#define TEST(name, T)	\
static void test ## name(T a) { return fung(a); }

TEST(f, float)
TEST(d, double)
TEST(l, long double)

/*
 * check-name: generic-functions
 * check-command: test-linearize $file
 *
 * check-output-start
testf:
.L0:
	<entry-point>
	call        funf, %arg1
	ret


testd:
.L2:
	<entry-point>
	call        fund, %arg1
	ret


testl:
.L4:
	<entry-point>
	call        funl, %arg1
	ret


 * check-output-end
 */
