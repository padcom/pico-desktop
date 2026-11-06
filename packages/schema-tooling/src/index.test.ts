import { describe, it, expect } from 'vitest'
import { SCHEMA_CANDIDATES } from '.'

describe('schema tooling', () => {
  it('evaluates CDDL and JSON Schema', () => {
    expect(SCHEMA_CANDIDATES).toEqual(['cddl', 'json-schema'])
  })
})
