# Options

This file is a complement of man page for sparse but meant
for options not to be used by sparse itself but by the other
tools.

## Developer options:

### Select the passes

* '-f\<name-of-the-pass\>[-disable|-enable|=last]'

  If '=last' is used, all passes after the specified one are disabled.
  By default all passes are enabled.

  The passes currently understood are:
  * 'mem2reg'
  * 'optim'

### Internal Representation

* '-fdump-ir[=\<pass\>[,\<pass\>...]]'

  Dump the IR at each of the given passes.

  The passes currently understood are:
  * 'linearize'
  * 'mem2reg'
  * 'final'
