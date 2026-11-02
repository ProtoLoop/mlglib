# Code Review Guidelines

Review code for correctness, architectural fit, and clarity. Remain read-only
unless the user explicitly approves changes.

## Establish context

- Read the repository's agent instructions, architecture guidance, and coding
  conventions.
- Inspect the complete change, including new files, callers, nearby code, and
  relevant tests.
- Compare analogous systems before recommending a different pattern.
- Treat the current repository as authoritative. Verify earlier findings against
  the latest files.
- Respect the requested scope and explicitly deferred issues.

## Check correctness and lifetime

- Trace the full path from input through processing to output. Check that new
  data is actually populated, propagated, and consumed.
- Check initialization, bounds, integer overflow, narrowing conversions,
  alignment, and invalid arithmetic.
- Look for undefined behavior, dangling references, use after free, double
  destruction, and uninitialized reads.
- Check pointer, reference, iterator, and view validity after moves, container
  growth, asynchronous work, and owner destruction.
- Verify ownership, destruction order, copy/move behavior, and the allowed state
  of moved-from objects.
- Check thread affinity, synchronization, races, callback lifetimes, and
  cancellation.
- Follow failure paths. Check error propagation, cleanup, partial updates,
  retries, and whether failed operations leave usable state.
- Check empty inputs, boundary values, repeated calls, and changes to resources
  or configuration.
- Verify serialized and GPU layouts, units, coordinate spaces, and compatibility
  versions.

## Check contracts and construction

- Confirm that construction establishes the invariants the type promises.
- Check that callers cannot bypass required validation.
- Validate inputs before dereferencing handles, creating dependent resources,
  or performing side effects.
- Identify states that the API permits but cannot safely use. Prefer types that
  prevent those states.
- Use recoverable errors for expected failures and assertions for programming
  errors, following repository conventions.
- Look for facts that can be enforced at compile time through types, constraints,
  exhaustive enum handling, or static assertions.
- Keep runtime checks for facts that depend on runtime data. Do not replace
  necessary checks with assumptions.

## Check consistency and architecture

- Compare related APIs for naming, parameter order, ownership, return types,
  validation, and lifecycle.
- Check declarations, definitions, and call sites together when signatures change.
- Look for stale diagnostics, labels, comments, examples, and documentation.
- Flag comments containing `DO NOT SUBMIT` or `DONOTSUBMIT`, matching case
  insensitively.
- Check whether responsibilities remain clear and dependencies follow established
  boundaries.
- Identify duplicated work, unused retained resources, and unexpected side effects
  on shared state.
- Distinguish justified differences from accidental divergence. Similar classes
  do not automatically need identical implementations.
- Recommend shared machinery only when it removes meaningful duplication or
  enforces a common rule.
- Check whether changes require architecture documentation, format-version,
  migration, or third-party notice updates.
- Suggest updates to `docs/architecture/decisions.md` when a change makes or
  revises a significant architectural choice. Supersede accepted decisions rather
  than rewriting them.

## Validate findings

- Support each finding with a concrete code location and an explanation of when
  it matters.
- Describe the trigger, incorrect behavior, and consequence. Give a small example
  when useful.
- Distinguish confirmed defects, plausible risks, incomplete features, and
  optional improvements.
- Avoid speculative warnings without a credible path to failure.
- Recommend focused validation for the behavior at risk. Run only checks
  permitted by the repository's approval rules.
- State what was inspected or tested and what remains unverified. Never imply
  that inspection proves a build or runtime behavior.

## Report and follow up

- Number every issue so the user can refer to it by number.
- Lead with correctness and safety issues, then contracts, consistency, and
  documentation.
- Keep findings concise and actionable. Explain the consequence and suggest the
  smallest proportionate correction.
- Distinguish required fixes from optional improvements.

## Review in iterations

- Expect the user to fix a subset of issues and request another review.
- Interpret issue numbers against the most recent review.
- Inspect each claimed fix, its callers, and related behavior. Check for
  regressions and newly exposed issues.
- Briefly summarize which previous issues are resolved or partially resolved,
  using their previous numbers.
- Highlight newly discovered issues explicitly.
- Re-render the complete list of outstanding issues, including unchanged issues
  and the remaining portions of partial fixes.
- Give that list fresh, consecutive numbers starting at 1. State that these
  numbers replace the previous numbering.
- Remove resolved issues from the outstanding list. Keep explicitly deferred or
  excluded issues in a separate note.
- Do not report only the latest findings or require the user to combine several
  responses to understand what remains.
- If no issues remain, say so and state any validation limits.
