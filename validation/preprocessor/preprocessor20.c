#include "preprocessor20.h"
#define X
#define Y
#include "preprocessor20.h"
/*
 * check-name: Preprocessor #20
 *
 * check-command: sparse -E $file
 * check-exit-value: 0
 *
 * check-output-start

A
B
 * check-output-end
 */
