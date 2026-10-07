# ADR-0031: Tiered Native Validation

- Status: Accepted
- Date: 2026-10-07
- Supersedes: ADR-0010

## Context

ADR-0010 established native, QEMU, integration, and stress layers and set a
two-second native-suite target while the repository contained only the early
kernel foundations.

The native graph now includes deterministic scheduler, endpoint, and IPC
regressions plus replayable state models with thousands of transitions under
ASan and UBSan. Running every native model after every small edit preserves
coverage but makes the normal feedback loop needlessly slow. The complete
native gate measured about 24 seconds on the reference development host, with
persistent models accounting for about 20 seconds.

The project still requires every accepted gate. The immediate problem is that
long-running native models are coupled to the deterministic development loop.

## Decision

The layered strategy and diagnostic requirements from ADR-0010 remain in
force. Validation is divided into three explicit execution tiers.

### Fast tier

`test-unit-fast` contains deterministic native regressions and Python harness
tests but excludes tests labeled `slow`. Its incremental execution budget is
normally six seconds on the reference development host.

The persistent endpoint and IPC models currently run through
`test-ipc-model`. Additional slow models join an equivalent labeled target
rather than returning to the fast executable.

### Complete native tier

`test-unit` remains the complete native gate and includes both fast tests and
all slow models. It remains required for `main`, milestone acceptance, and
changes to shared native state-machine or model infrastructure.

QEMU component selection remains governed by the testing strategy and each
task's affected mechanisms. Automating changed-path QEMU selection is a
separate follow-up decision and implementation.

## Consequences

- Ordinary development receives deterministic feedback in seconds rather than
  rerunning long models and unrelated guests.
- Persistent models remain mandatory in the complete native gate.
- Full validation remains unchanged in coverage.
- Timing budgets describe incremental execution; clean compilation time is
  tracked separately and may benefit from an optional compiler cache later.

## Alternatives considered

### Remove overlapping QEMU readiness checks

Rejected. Repeated readiness evidence proves that each independently compiled
test image reaches the same valid production baseline.

### Run every native model for every edit

This maximizes repetition rather than information and makes small-PR feedback
unnecessarily expensive.

### Reduce model transition counts

Rejected. The accepted endpoint and IPC ADRs require their replayable
transition coverage; moving those models out of the fast tier preserves that
evidence without weakening it.
