import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterAll, describe, expect, it } from 'vitest'
import { create } from './create'
import type { Output } from './io'

const framework = fileURLToPath(new URL('../../..', import.meta.url))
const scratch = mkdtempSync(join(tmpdir(), 'pd-create-'))

afterAll(() => rmSync(scratch, { recursive: true, force: true }))

function run(dir: string, id?: string) {
  const out: string[] = []
  const io: Output = { out: t => out.push(t), err: t => out.push(t) }

  return { code: create({ dir, id, framework }, io), out: out.join('\n') }
}

describe('create', () => {
  it('scaffolds an app linked to the framework, its contract generated', () => {
    const dir = join(scratch, 'my-app')
    const { code, out } = run(dir, 'com.example.my-app')
    expect(code, out).toBe(0)
    const pkg = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')) as Record<string, Record<string, string>>
    expect(pkg.devDependencies?.['pico-desktop']).toBe(`link:${join(framework, 'tooling/cli')}`)
    expect(readFileSync(join(dir, 'pico-desktop.config.ts'), 'utf8')).toContain("id: 'com.example.my-app'")
    expect(readFileSync(join(dir, 'apps/backend/project.json'), 'utf8')).toContain('"name": "my-app/backend"')
    expect(existsSync(join(dir, '.gitignore'))).toBe(true)
    expect(existsSync(join(dir, 'apps/frontend/src/generated/app.ts'))).toBe(true)
    expect(readFileSync(join(dir, 'apps/backend/generated/app.pi'), 'utf8')).toContain('from "pico-desktop/communicator"')
  })

  it('refuses a directory that isn\'t empty, and bad names', () => {
    expect(run(join(scratch, 'my-app')).code).toBe(1)
    expect(run(join(scratch, 'My App')).out).toContain('isn\'t a valid app name')
  })
})
