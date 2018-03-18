# Options

This file is a complement of sparse's man page meant to
document options only useful for development on sparse itself.

## Developer options:

### Select the passes

* '-f\<name-of-the-pass\>[-disable|-enable|=last]'

  If '=last' is used, all passes after the specified one are disabled.
  By default all passes are enabled.

  The passes currently understood are:
  * 'mem2reg'
  * 'optim'

### Debugging

* '-fdump-ir[=\<pass\>[,\<pass\>...]]'

  Dump the IR at each of the given passes.

  The passes currently understood are:
  * 'linearize'
  * 'mem2reg'
  * 'final'
