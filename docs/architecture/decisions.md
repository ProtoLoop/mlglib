# Architecture Decisions

This document records important architectural choices when the reasons behind
them will be useful later.

Add a decision when it affects several parts of the system, limits future
choices, or is likely to be questioned again after the original context has
been forgotten. Routine implementation choices belong in the code or in a
pattern.

Each decision should include:

- a title, date, and status.
- the situation that required a choice.
- what we decided.
- the alternatives we considered.
- the good and bad consequences.
- links to related principles, patterns, and older decisions.

An accepted decision is part of the project's history. If the decision changes,
add a new entry and mark the old one as superseded instead of rewriting it.

## Scene nodes own light transforms

Recorded: 2026-11-01. Status: Accepted.

Light position and direction come from the owning scene node rather than separate
light fields. This keeps hierarchy updates consistent, but moving a light also
moves other components and descendants attached to that node.

Related: [Make it clear who is responsible](principles.md#make-it-clear-who-is-responsible),
[Asset Flow](asset-flow.md).

## GPU resource wrappers preserve validity after moves

Recorded: 2026-11-01. Status: Accepted.

Typed GPU buffer and texture-target wrappers copy reference-counted handles when
moved. This keeps both objects usable, at the cost of reference-count operations
and retaining the source’s resource reference. Consuming staging buffers are
excluded.

Related: [Make invalid states unrepresentable](principles.md#make-invalid-states-unrepresentable),
[Copy and move behavior](conventions.md#copy-and-move-behavior).
