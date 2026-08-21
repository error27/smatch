---
name: update-smatch-validation
description: Update one Smatch validation test so its expected output matches intentional current checker behavior. Use when a named file under validation/ is out of date, a focused Smatch test reports an output mismatch, or the user asks to refresh expected validation output without changing checker code.
---

# Update Smatch Validation

Update one named validation test at a time. Treat the current output as a
candidate expectation, not automatically as correct behavior.

1. Read the repository `AGENTS.md` completely and follow its patch, testing,
   commit-message, and AI-attribution rules.
2. Inspect `git status --short`, `git diff`, the complete validation file, and
   the code responsible for the changed output. Preserve unrelated work.
3. From `validation/`, run the focused test before editing:

       ./test-suite single <test-file.c>

4. Record the exact mismatch. Decide whether the new output is an intentional
   consequence of current code or a Smatch regression. If it appears incorrect
   or uncertain, do not update the expectation; diagnose or report the
   possible bug instead.
5. If the current behavior is correct, change only the stale expected output
   and any directly affected test explanation. Do not alter test inputs merely
   to make the test pass.
6. Run the same focused test again. Require it to pass, then run
   `git diff --check` and inspect the complete diff.
7. Prepare the patch in the format required by `AGENTS.md`. Keep the validation
   update separate from unrelated checker changes. Include the exact failing
   before output and passing after output in the commit message.

If the build or focused test cannot run, state the limitation and do not claim
the validation passed.
