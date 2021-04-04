typedef int word;

void foo(word x);

void foo(x)
	word x;
{ }

/*
 * check-name: knr-attr-crash
 * check-command: sparse -Wno-old-style-definition $file
 */
