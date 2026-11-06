/**
 * Contract tooling: from a language-neutral contract to TypeScript types, Pico types and runtime
 * validators. The format is chosen by the F2 experiment (docs/adr/0005-schema-format.md).
 */

/** The contract formats under evaluation. */
export const SCHEMA_CANDIDATES = ['cddl', 'json-schema'] as const

export type SchemaCandidate = typeof SCHEMA_CANDIDATES[number]
