import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, relative, win32 } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterAll, describe, expect, it } from 'vitest'
import { create, linkTo } from './create'
import type { Output } from './io'

const framework = fileURLToPath(new URL('../../..', import.meta.url))
const scratch = mkdtempSync(join(tmpdir(), 'pd-create-'))

afterAll(() => rmSync(scratch, { recursive: true, force: true }))

function run(dir: string, id?: string, template?: string) {
  const out: string[] = []
  const io: Output = { out: t => out.push(t), err: t => out.push(t) }

  return { code: create({ dir, id, framework, template }, io), out: out.join('\n') }
}

describe('create', () => {
  it('scaffolds an app linked to the framework, its contract generated', () => {
    const dir = join(scratch, 'my-app')
    const { code, out } = run(dir, 'com.example.my-app')
    expect(code, out).toBe(0)
    const pkg = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')) as Record<string, Record<string, string>>
    // Relative links, from each package.json that holds one.
    expect(pkg.devDependencies?.['pico-desktop']).toBe(`link:${relative(dir, join(framework, 'tooling/cli'))}`)
    const frontendPkg = readFileSync(join(dir, 'apps/frontend/package.json'), 'utf8')
    const frontend = JSON.parse(frontendPkg) as Record<string, Record<string, string>>
    const protocol = relative(join(dir, 'apps/frontend'), join(framework, 'packages/protocol'))
    expect(frontend.dependencies?.['@pico-desktop/protocol']).toBe(`link:${protocol}`)
    expect(frontend.dependencies?.['@pico-desktop/protocol']).not.toMatch(/^link:\//)
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

describe('create --template vue-ts', () => {
  const dir = join(scratch, 'vue-app')

  it('scaffolds the Vue template with its placeholders filled and its contract generated', () => {
    const { code, out } = run(dir, 'com.example.vue-app', 'vue-ts')
    expect(code, out).toBe(0)
    const app = readFileSync(join(dir, 'apps/frontend/src/App.vue'), 'utf8')
    expect(app).toContain('<h1>vue-app</h1>')
    expect(app).not.toMatch(/\{\{(?:name|appName|appId)\}\}/u)
    const text = readFileSync(join(dir, 'apps/frontend/package.json'), 'utf8')
    const frontend = JSON.parse(text) as Record<string, Record<string, string>>
    expect(frontend.dependencies?.vue).toBeDefined()
    expect(frontend.dependencies?.['@pico-desktop/communicator']).toMatch(/^link:/u)
    expect(readFileSync(join(dir, 'pico-desktop.config.ts'), 'utf8')).toContain("id: 'com.example.vue-app'")
    expect(existsSync(join(dir, 'apps/frontend/src/generated/app.ts'))).toBe(true)
    expect(existsSync(join(dir, '.gitignore'))).toBe(true)
  })

  it('refuses a template that doesn\'t exist', () => {
    expect(run(join(scratch, 'other'), undefined, 'react').out).toContain('isn\'t a template')
  })
})

describe('linkTo', () => {
  it('uses forward slashes, for JSON and pnpm', () => {
    expect(linkTo('C:\\work\\app', 'C:\\work\\fw\\packages\\x', win32.relative)).toBe('link:../fw/packages/x')
  })

  it('stays valid JSON when the two are on different drives', () => {
    const link = linkTo('C:\\tmp\\app', 'D:\\fw\\packages\\x', win32.relative)
    expect(link).toBe('link:D:/fw/packages/x')
    expect(() => JSON.parse(`"${link}"`)).not.toThrow()
  })
})
