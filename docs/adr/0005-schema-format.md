# ADR-0005: Contracts are written in CDDL

**Status:** Accepted (F2 experiment, 2026-11-06) · **Date:** 2026-11-06

## Context
One language-neutral contract should produce TypeScript types, Pico types and runtime validators
for messages crossing trust boundaries. It has to express CBOR byte strings, unsigned 64-bit
integers, optional fields, enums, unions, nested and recursive types, error payloads, and the
service registry (methods, events, subscriptions). Schema languages alone don't define the registry.

## Experiment
`packages/schema-tooling` holds:
- **One test contract in both formats:** `contracts/experiment.cddl`, and `experiment.schema.json` + `experiment.services.json`. It covers sized and 64-bit integers, byte strings, optional members, a closed enum, a tagged union, recursion, an error payload, 4 methods, an event and a subscription.
- **Two front-ends, one model:** `from-cddl.ts` (with the `cddl` package's parser) and `from-json-schema.ts` both produce one contract model (`ir.ts`).
- **One generator per language:** `generate-ts.ts` writes TypeScript types, validators and the service registry; `generate-pico.ts` writes Pico structs, enums and payload-enum unions, with decoders over communicator-pico's `payload.pi`.
- **One corpus:** 27 valid and invalid payloads (`corpus.ts`), run through both the TypeScript validators and the compiled Pico decoders (`communicator-pico/experiment.test.pi`).

## Results
- **Expressiveness:** equal for this contract. Both front-ends produce the **same model** (`toEqual`), and so byte-identical generated code. The generated files are snapshots, so generation is deterministic.
- **Generated code works in both languages:** the TypeScript validators and the Pico decoders (compiled with pico-lang main + #82/#84/#90) give the corpus's verdict for **all 27 cases**:
  - closed maps, integer widths and bounds;
  - integral floats arriving as CBOR integers, and uint64 as `bigint`;
  - union tags, recursion depth.
- **Fit for CBOR:**
  - **CDDL (RFC 8610)** says it all natively: `bstr`, `uint .size n`, `uint .le n`, `?` optional members, choices, recursion, closed maps. The contract is **~45 lines**.
  - **JSON Schema** needs a **non-standard extension** (`x-cbor: bytes`, `x-cbor: uint64`) for exactly what the protocol relies on. JSON numbers can't even state 2^64−1 exactly. Unions are `oneOf` plus `const` discriminators, and the registry is a second file. That's **~150 lines**.
- **Tooling:**
  - **JSON Schema's ecosystem is large**, but it targets JSON values: Ajv can't validate `Uint8Array` or `bigint` without custom keywords, and no generator writes Pico. The advantage mostly doesn't transfer.
  - **CDDL's npm parser** (`cddl` 0.23, webdriverio, used for WebDriver BiDi) works but is rough:
    - it doesn't support generics (`method<req, res>`);
    - it reads a quoted string inside an array as a rule reference;
    - its AST is loosely and inconsistently typed.

  Our own generators are needed for Pico either way.

## Decision
Contracts are written in **CDDL**, with these conventions:
- **Maps are closed:** unknown members are rejected, as in CDDL without `* tstr => any`.
- **Integers:** `uint .size 1|2|4` are sized integers; `uint`/`.size 8` is 64-bit; `.le n` is a bound.
- **Enums and unions:** a choice of text literals is an enum. A choice of maps with one member that has a different literal value in each is a tagged union.
- **The registry:** one `services` map, with entries `["method", request, response]`, `["event", payload]` or `["subscription", params, value]`.

  Tagged arrays are used because the parser lacks generics; `method<request, response>` would read better and is the first thing to improve, upstream or in our own parser.
- **The parser is pinned** (`cddl` 0.23.0) and wrapped by `from-cddl.ts`. If its quirks get in the way, it's replaced by our own parser for the subset; nothing else depends on it.

## Consequences
- **Next:** F3 builds the Communicators on the generated TypeScript (`Services` registry, validators) and the generated Pico decoders; encoders and handlers come with it.
- **The JSON Schema front-end stays as experiment evidence only.** It isn't a supported input.
- **Found along the way (filed):**
  - in Pico, a string-literal union can't be assigned to its optional type (pico-lang #92; the generator uses Pico enums instead);
  - in Pico, an enum variant named `Map` can't be matched with a `Map { … }` pattern (pico-lang #89).
