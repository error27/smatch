Developing Smatch checks
=======================

This is an introduction to writing and debugging Smatch checks.  Smatch has
many helpers and several kinds of cross-function analysis.  Start with a
small check and use a similar existing check as a guide instead of trying to
learn the whole code base first.

The source tree
---------------

The source tree contains three broad groups of files:

``check_*.c``
  Individual checks.  These normally register callbacks and emit a warning
  when a suspicious pattern is found.

``smatch_*.c``
  Core analysis modules shared by checks.  They provide state tracking,
  ranges, implied values, modification hooks, function hooks and database
  support.

Other ``*.c`` and ``*.h`` files
  The bundled Sparse parser and its support code.

``smatch_flow.c`` controls the traversal of parsed code.  Searching it for
``__pass_to_client()`` shows where the generic hooks are called.
``smatch_function_hooks.c`` implements hooks for named function calls.  The
comment at the start of that file describes the function-hook variants and
their ordering.  Declarations used by checks are collected in ``smatch.h``.

Some common data types are:

``struct expression``
  A C expression, such as an assignment, comparison, call or dereference.

``struct statement``
  A C statement, such as an ``if``, loop, return or compound statement.

``struct symbol``
  A function, variable, type or another named C object.

``struct smatch_state``
  A state defined by a check, such as ``freed``.

``struct sm_state``
  Smatch's record associating an owner, name and symbol with a state.  It
  also contains merge history and a list of possible states.

``struct stree``
  A collection of ``sm_state`` records.  The current collection of states is
  called the current stree.

Writing a first check
---------------------

The following example warns when a pointer is dereferenced after it was
passed to ``kfree()``.  Smatch already has more complete freed-memory
tracking; this deliberately small example demonstrates the basic pieces of a
check.

Every check includes ``smatch.h`` and stores the unique ID assigned when the
check is registered::

  #include "smatch.h"

  static int my_id;

Keep the states for a check simple.  Most checks only need to define the
interesting state; Smatch supplies the shared ``undefined`` and ``merged``
states::

  STATE(freed);

The param/key API is the preferred way to handle function parameters.  This
callback marks the name and symbol resolved from a parameter and key::

  static void match_free(struct expression *expr, const char *name,
                         struct symbol *sym, void *data)
  {
          set_state(my_id, name, sym, &freed);
  }

The dereference callback checks the possible states, not only the current
state.  This warns when the pointer was freed on any path which reaches the
dereference::

  static void match_dereference(struct expression *expr)
  {
          char *name;

          if (!expr_has_possible_state(my_id, expr, &freed))
                  return;

          name = expr_to_str(expr);
          sm_warning("pointer '%s' was freed", name);
          free_string(name);
  }

An assignment to the pointer makes the old state stale.  Register a
modification hook to clear it::

  static void reset_state(struct sm_state *sm,
                          struct expression *mod_expr)
  {
          set_state(my_id, sm->name, sm->sym, &undefined);
  }

The registration function must have the same name as the file without the
``.c`` suffix.  For ``check_dereferencing_kfreed.c`` it is::

  void check_dereferencing_kfreed(int id)
  {
          my_id = id;

          add_function_param_key_hook("kfree", &match_free,
                                      0, "$", NULL);
          add_dereference_hook(&match_dereference);
          add_modification_hook(my_id, &reset_state);
  }

The Makefile builds every ``check_*.c`` file.  ``build_check_list.sh``
generates ``smatch_checks.h`` from the filenames, so there is no central list
to edit when adding a check.

Testing a check
---------------

Tests live under ``validation/``.  A small test for the example check could
start with::

  void kfree(void *p);
  _Bool frob(void);

  void test_func(int *p)
  {
          if (frob())
                  kfree(p);
          *p = 100;
  }

Add the test-suite directives and exact expected output at the end of the
file::

  /*
   * check-name: dereferencing kfreed memory
   * check-command: smatch -p=kernel sm_dereferencing_kfreed.c
   *
   * check-output-start
  sm_dereferencing_kfreed.c:8 test_func() warn: pointer 'p' was freed
   * check-output-end
   */

Run one test from the ``validation`` directory::

  ./test-suite single sm_dereferencing_kfreed.c

Run the complete validation suite from the source root with::

  make check

A useful test has code which must warn and nearby code which must not warn.
Real bugs are also valuable test cases because they exercise details which
are often missing from a contrived example.

Hooks and modules
-----------------

A check normally does little work during registration.  It registers
callbacks for the events it needs.  Generic hooks include expression,
statement, condition, assignment, function-call, inline-function and
end-of-function events.  Search ``smatch.h`` for ``enum hook_type`` and search
the source for users of a hook to see its callback signature and timing.

Function hooks have more specialized variants.  Regular function hooks run
for every call to a named function.  Return-implies hooks run only when a
return value is possibly or definitely within a specified range.  Assignment
hooks run when a function call is assigned, and macro-assignment hooks do the
same for macros.  Early and late variants provide explicit ordering when one
module depends on information produced by another.

Do not depend on registration order to sequence checks.  Use the early or
late hook designed for that purpose.

Smatch analyzes preprocessed code, so macros can be awkward to recognize.
Use ``get_macro_name()`` when a check genuinely needs to treat a macro
differently.

The numeric helpers express different degrees of certainty:

``get_value()``
  Get a constant value.

``get_implied_value()``
  Get a value which Smatch considers known.

``get_implied_rl()``
  Get the known range list.  This can still be the whole range.

``get_absolute_rl()``
  Get the possible range list.  This always returns a range.

``smatch_extra.c`` and ``smatch_math.c`` handle much of the numeric analysis.
``smatch_comparison.c`` tracks relationships between variables.
``smatch_ssa.c`` tracks aliases using a form of single static assignment.  If
``x = p`` and then ``x`` is freed, SSA can connect that operation back to the
corresponding version of ``p``.

Prefer several small modules over one check which records and reports every
kind of information itself.  Producer modules can collect facts, tracker
modules can maintain state and a final check can decide when to warn.

Merging states
--------------

Branches are visible in C source, but the point where paths merge is often
implicit::

  if (x == 1) {
          y = 1;
          kfree(p);
  } else {
          y = 3;
  }
  /* The paths merge here. */

Smatch stores each path's states in an stree.  When it merges two strees,
every state on one side needs a matching state on the other.  If ``p`` is
``freed`` on the true path and has no state on the false path, the default
unmatched state on the false path is ``undefined``.  Merging ``freed`` and
``undefined`` produces the state ``merged``.  The ``possible`` list in the
resulting ``sm_state`` still contains ``freed`` and ``undefined``.

This is why a check which cares whether something happened on any incoming
path uses ``expr_has_possible_state()`` or walks ``sm->possible``.  Testing
only the current state would see ``merged`` and lose the useful distinction.

Three hooks customize merging:

``add_unmatched_state_hook()``
  Supplies a state other than ``undefined`` when a state exists on only one
  side.  For example, freeing a parameter only when it is non-NULL can still
  mean that the parameter is released on every relevant path.

``add_merge_hook()``
  Combines two states into a check-specific result instead of ``merged``.
  Smatch still records that a merge occurred.

``add_pre_merge_hook()``
  Adjusts a state using other analysis immediately before the merge.  For
  example, a module can discard an uninitialized state from an impossible
  path.

Only add custom merge behavior when the default state and possible-state list
cannot express what the check needs.  Merge hooks affect every branch in the
function and can make incorrect assumptions spread far from their source.

The param/key API
-----------------

A param/key identifies a value relative to a function parameter.  It consists
of a zero-based parameter number and a key describing the value below it.
For example:

``0/$``
  Parameter zero itself.

``0/$->foo->bar``
  The ``bar`` member below ``foo`` in parameter zero.

The param/key API resolves this description to a name and symbol before it
calls the check.  This removes repeated expression parsing from callbacks and
allows the same callback to handle hard-coded function models and facts read
from the cross-function database.

For a direct function model, register a callback with::

  add_function_param_key_hook("kfree", &match_free, 0, "$", NULL);

A real check normally has a table of functions, parameter numbers and keys,
then registers the same callback for every entry.

To consume matching return-state facts from the cross-function database, use::

  select_return_param_key(FREED, &match_free);

The API works for states tied to a variable.  It is not appropriate for
global facts such as whether preemption is disabled.  It is also best effort:
Smatch cannot always connect an expression back to a function parameter.

The cross-function database
---------------------------

The cross-function database lets analysis in one translation unit use facts
learned in another.  It is optional.  Build the Linux kernel database with::

  smatch_scripts/build_kernel_data.sh

This creates ``smatch_db.sqlite``.  Building it takes hours.  Information
propagates farther through the call tree on each rebuild: one run can record
what ``foo()`` passes to ``bar()``, and the next can use that fact while
analyzing what ``bar()`` passes to ``baz()``.  Several rebuilds may be needed
before long call chains are well populated.

The database is deliberately best effort.  Smatch limits or deletes facts
which would consume too much space.  It uses the same interfaces with an
in-memory database while analyzing inline functions.

The most important tables for checks are:

``caller_info``
  Facts about values passed to a function.

``return_states``
  Facts associated with paths and values returned from a function.

When a function has one ``return ret`` statement but ``ret`` represents
different results on different paths, Smatch splits the return into meaningful
return states before recording it.

To store state associated with a parameter at a return, register a callback::

  add_return_info_callback(my_id, free_info_callback);

The callback can write a typed fact with::

  sql_insert_return_states(return_id, return_ranges, FREED,
                           param, printed_name, "");

The integer type, such as ``FREED``, identifies the producer and consumer of
the record.  The parameter number and key identify the affected value.  A
consumer registers ``select_return_param_key()`` for that type.

Caller information uses the corresponding callback and selection APIs::

  add_caller_info_callback(my_id, caller_info_callback);
  select_caller_name_sym(&select_caller_info, MY_TYPE);

Use ``smatch_data/db/smdb.py`` to inspect the database.  For example::

  smatch_data/db/smdb.py m88e1318_led_blink_set
  smatch_data/db/smdb.py return_states m88e1318_led_blink_set

Besides supporting warnings, the database can show call trees, function
pointer implementations, where struct members are set and how functions are
called.

Debugging checks
----------------

Smatch operates on preprocessed code.  When a hook does not see the expression
you expect, inspect the preprocessed file first.  For a kernel file::

  make drivers/foo/bar.i
  vim drivers/foo/bar.i

Confirm that the check was built and registered.  A ``check_*.c`` file should
appear in the generated ``smatch_checks.h`` after building.

For targeted debugging, include ``check_debug.h`` in the code being analyzed.
An absolute path is convenient when analyzing the kernel::

  #include "/home/me/src/smatch/check_debug.h"

This may require ``CONFIG_WERROR=n`` in the kernel configuration.  Debug
helpers can be inserted directly around the code of interest::

  __smatch_cur_stree();
  __smatch_states("check_name");
  __smatch_implied(variable);
  __smatch_about(variable);

``__smatch_cur_stree()`` prints the current stree,
``__smatch_states()`` limits output to one check, ``__smatch_implied()``
prints the implied value and ``__smatch_about()`` prints several kinds of
information about an expression.

Global debug output is usually too noisy::

  __smatch_debug_on();
  /* code of interest */
  __smatch_debug_off();

Local debugging is more useful.  Mark a region in the analyzed source::

  __smatch_local_debug_on();
  if (condition) {
          __smatch_local_debug_off();
          operation();
  }

Then make a hook conditional on ``local_debug``::

  if (local_debug)
          sm_msg("%s: hook called. expr='%s'",
                 __func__, expr_to_str(expr));

Database tracing can be enabled with ``__smatch_debug_db_on()`` and disabled
with ``__smatch_debug_db_off()``, but its output is extensive.  Reduce the
problem with the state and local-debug helpers first.

Further reading
---------------

This guide is based on the following articles:

* `First Smatch Check <https://staticthinking.wordpress.com/2023/04/25/first-smatch-check/>`_
* `Merging States <https://staticthinking.wordpress.com/2023/04/25/merging-states/>`_
* `The Cross Function DB <https://staticthinking.wordpress.com/2023/05/02/the-cross-function-db/>`_
* `The Param/Key API <https://staticthinking.wordpress.com/2023/05/02/the-param-key-api/>`_
* `Smatch hooks and modules <https://staticthinking.wordpress.com/2023/05/02/smatch-hooks-and-modules/>`_
* `Debugging Smatch Checks <https://staticthinking.wordpress.com/2023/05/02/debugging-smatch-checks/>`_

Questions and patches should be sent to <smatch@vger.kernel.org>.
