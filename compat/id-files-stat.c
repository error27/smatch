#include "../token.h"
#include <sys/stat.h>

int identical_files(struct stream* s, struct stat *st, const char * name)
{
	return s->dev == st->st_dev && s->ino == st->st_ino;
}
