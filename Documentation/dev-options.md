# sparse - extra options for developers

## SYNOPSIS
`tools` [`options`]... `file.c`

## DESCRIPTION

This file is a complement of sparse's man page meant to
document options only useful for development on sparse itself.

## DEVELOPER OPTIONS

-f<name-of-the-pass>[-disable|-enable|=last]

  If `=last` is used, all passes after the specified one are disabled.
  By default all passes are enabled.

  The passes currently understood are:

  * `mem2reg`
  * `optim`

### DEBUGGING

-fdump-ir[=pass[,pass...]]

  Dump the IR at each of the given passes.

  The passes currently understood are:

  * `linearize`
  * `mem2reg`
  * `final`

  The default pass is `linearize`.

-v<debug-flag>

  Add or display some debug info. The flag can be one of:

  * `dead`: annotate dead pseudos.
  * `entry`: dump the IR after all optimization passes.
