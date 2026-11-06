/**
 * Contract tooling (ADR-0005): CDDL contracts → TypeScript types, validators and the service
 * registry, and Pico types with decoders.
 */
export { type Contract, type Field, type Service, type Type, bitsFor } from './ir'

export { CddlReader, readCddl } from './from-cddl'

export { TypeScriptWriter, generateTypeScript, typeName } from './generate-ts'

export { generatePico } from './generate-pico'
