// Every framework package ships the license text (MIT requires it with the code; npm includes a
// package's LICENSE whatever its `files` say), and it's the repository's.
import { readFileSync, readdirSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { describe, expect, it } from 'vitest'

const root = resolve(import.meta.dirname, '../../..')
const packages = ['packages', 'tooling']
  .flatMap(group => readdirSync(join(root, group)).map(name => join(group, name)))
  .filter(dir => readdirSync(join(root, dir)).includes('package.json'))

describe('licenses', () => {
  it.each(packages)('%s has the repository\'s LICENSE', dir => {
    expect(readFileSync(join(root, dir, 'LICENSE'), 'utf8')).toBe(readFileSync(join(root, 'LICENSE'), 'utf8'))
  })
})
