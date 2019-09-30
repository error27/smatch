#ifndef __has_feature
__has_feature()??? Quesako?
#define __has_feature(x) 0
#else
"has __has_feature(), yeah!"
#endif

#if __has_feature(not_a_feature)
#error "not a feature!"
#endif

/*
 * check-name: has-feature
 * check-command: sparse -E $file
 *
 * check-output-start

"has __has_feature(), yeah!"
 * check-output-end
 */
