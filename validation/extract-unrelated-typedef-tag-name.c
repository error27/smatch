/*
 * check-name: extract struct with an unrelated typedef of the same name
 * check-command: extract --last-func $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.last
 *
 * check-output-start
extract: wrote extract-unrelated-typedef-tag-name.c.extract.last
 * check-output-end
 */

struct path;
struct path { void *mnt; };
struct fs_struct {
	int users;
	struct path root;
};
typedef char *path;

int fs_users(struct fs_struct *fs)
{
	return fs->users;
}
