_Static_assert([__WCHAR_TYPE__] == [unsigned short], "short wchar");

/*
 * check-name: short-wchar
 * check-command: sparse -fshort-wchar $file
 */
