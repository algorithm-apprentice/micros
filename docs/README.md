# Documentation

This directory contains the design baseline for `micros`.

## Architecture

- [System overview](architecture/system-overview.md) defines the target system,
  trust boundary, component responsibilities, boot phases, and invariants.
- [Development dependency DAG](architecture/development-dag.md) converts the
  cyclic runtime architecture into an acyclic implementation sequence.

## Research

- [MINIX dependency analysis](research/minix-dependency-analysis.md) records the
  source baseline, component relationships, strongly connected component, and
  bootstrap lessons used to derive the `micros` design.

## Planning and verification

- [Roadmap](roadmap.md) defines milestones, exit criteria, deferred scope, and
  major risks.
- [Testing strategy](testing-strategy.md) defines the host, QEMU, integration,
  stress, and observability layers.

## Decisions

- [ADR index](adr/README.md) lists all architecture decisions and their current
  status.

All project documentation, source code, identifiers, comments, tests, and
commit messages are written in English.
