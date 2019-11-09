static int i;
static typeof(i) *ptr;

/*
 * check-name: eval-typeof0
 * check-command: test-show-type $file
 *
 * check-output-ignore
 * check-output-excludes: unknown type
 */
