#define __restrict_arr __restrict

struct aiocb64;
struct sigevent;

extern int lio_listio64 (int __mode,
			 struct aiocb64 *__const __list[__restrict_arr],
			 int __nent, struct sigevent *__restrict __sig);

/*
 * check-name: restrict array attribute
 */
