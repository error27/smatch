extern int someval(void);

static void func (int x)
{
	if (x > someval())
		goto end;
	switch (x) { case 0: }
	switch (x) { case 1 ... 9: }
	switch (x) { default: }
end:
}

/*
 * check-name: label-positioning
 *
 * check-error-start
label-positioning.c:7:30: warning: statement expected after case label
label-positioning.c:8:36: warning: statement expected after case label
label-positioning.c:9:31: warning: statement expected after case label
label-positioning.c:11:1: warning: statement expected after label
 * check-error-end
 */
