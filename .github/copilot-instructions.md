# micros Copilot Instructions

Read `AGENTS.md` first, then follow its required context order. In particular,
read the system overview, development DAG, roadmap, relevant ADRs, testing
strategy, and AI-native workflow before changing the repository.

Follow every Accepted ADR relevant to the files being changed.

Determine the current role before acting:

- In an authoring task, use documentation-first and test-first development,
  work on one dependency-ready task, keep its non-goals, create small green
  commits, and obtain an independent review before merge.
- In an independent review task, remain read-only, inspect the complete current
  change, report only substantive evidence-backed findings, and never edit,
  commit, push, merge, implement, or request another review.

Do not duplicate canonical architecture text in this file. Update the source
document and reference it instead.
