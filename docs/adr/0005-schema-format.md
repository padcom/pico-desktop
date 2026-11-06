# ADR-0005: Contract format (CDDL vs JSON Schema + service manifest)

**Status:** Proposed (open); decided by the F2 experiment · **Date:** 2026-11-06

## Context
One language-neutral contract should produce TypeScript types, Pico types and runtime validators
for messages crossing trust boundaries. It has to express CBOR byte strings, unsigned 64-bit
integers, optional fields, unions, enums, nested and recursive types, errors, and the service
registry (methods, events, subscriptions), which schema languages alone don't define.

## Options
- **CDDL** (RFC 8610): made for CBOR (byte strings, integer ranges, tags are native); thinner
  tooling ecosystem.
- **JSON Schema + a small service manifest**: rich tooling; CBOR's binary and integer semantics
  need conventions on top (JSON Schema has no byte strings or uint64).

## Decision process
F2 models identical test contracts in both, generates compilable Pico and TypeScript code,
validates a shared CBOR corpus (including uint64 and byte strings) and compares maintainability
and generator determinism. No default is assumed. `@pico-desktop/schema-tooling` lists both as
`SCHEMA_CANDIDATES` until then.

## Consequences
Until F2 decides, hand-written contracts are used in tests only.
