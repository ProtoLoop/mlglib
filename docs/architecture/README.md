# Architecture

These documents explain how the project is put together and why. They give us
a shared reference for design work and help keep similar systems consistent.

Each file has a different job:

| Document | What it contains |
| --- | --- |
| [Principles](principles.md) | The main ideas we keep in mind while developing |
| [Patterns](patterns.md) | Approaches we use repeatedly to solve the same kind of problem |
| [Conventions](conventions.md) | How project code is expected to be written |
| [Decisions](decisions.md) | Important choices and the reasons behind them |
| [Architecture Backlog](backlog.md) | Architectural questions that still need a decision |
| [Asset Flow](asset-flow.md) | How assets become definitions and cooked resources, and how runtime loading will use them |

The categories are related but should stay separate. Principles guide our
judgment. Patterns show how we put those principles into practice. Conventions
record concrete expectations for project code. Decisions record why we chose
one direction over another. System descriptions such as Asset Flow explain how
the parts fit together and distinguish current behavior from planned work.

## Writing style

Write these documents for developers working in the code:

- Use direct, conversational language.
- Prefer familiar words and short sentences.
- Say what the project does or expects instead of using abstract architecture
  language.
- Keep technical terms when they add precision, and explain uncommon ones.
- Ground guidance in real project choices rather than generic software advice.
- State tradeoffs plainly.
- Keep principles short enough to remember. Put detailed rules and examples in
  patterns, decisions, or the system description.
- Remove wording that sounds authoritative but does not help someone make a
  decision.

## Keeping these documents useful

- Document choices that are intentional and likely to matter again. Do not
  catalog every implementation detail.
- Update a pattern when its rules or intended use change.
- Keep conventions concrete and consistent with the codebase.
- Do not rewrite the history in an accepted decision. Add a new decision and
  mark the old one as superseded.
- Link to representative code instead of copying large sections that will
  quickly become stale.
