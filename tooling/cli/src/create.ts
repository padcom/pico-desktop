// `pico-desktop create <dir>`: a new app from the vanilla-ts template, its contract already
// generated. Until the packages are published, the app links them from a framework checkout
// (`--framework`, by default the checkout this CLI runs from).
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs'
import { basename, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { resolveConfig } from './config'
import { generate } from './generate'
import type { Output } from './io'

const TEMPLATE = fileURLToPath(new URL(/* @vite-ignore */ '../../templates/vanilla-ts', import.meta.url))
const CHECKOUT = fileURLToPath(new URL(/* @vite-ignore */ '../../..', import.meta.url))

export interface CreateOptions {
  dir: string
  id?: string | undefined
  framework?: string | undefined
}

const NAME = /^[a-z0-9][a-z0-9-]*$/u

const isFramework = (dir: string) => existsSync(join(dir, 'packages', 'communicator-pico', 'project.json'))

/**
 * The template's placeholders for an app linked to a framework checkout.
 *
 * @param name the app's directory name
 * @param id the app's ID
 * @param framework the checkout
 * @returns placeholder → value
 */
export function placeholders(name: string, id: string, framework: string): Record<string, string> {
  const root = JSON.parse(readFileSync(join(framework, 'package.json'), 'utf8')) as { devDependencies: Record<string, string> }
  const pico = JSON.parse(readFileSync(join(framework, 'packages/communicator-pico/project.json'), 'utf8')) as { version: string }
  const link = (path: string) => `link:${join(framework, path)}`

  return {
    name,
    'appId': id,
    'appName': name,
    'dep:cli': link('tooling/cli'),
    'dep:communicator': link('packages/communicator-ts'),
    'dep:communicator-pico': link('packages/communicator-pico'),
    'dep:protocol': link('packages/protocol'),
    'version:communicator-pico': pico.version,
    'version:typescript': root.devDependencies.typescript ?? '5.9.3',
    'version:vite': root.devDependencies.vite ?? '8.3.6',
    'version:vitest': root.devDependencies.vitest ?? '5.1.0',
  }
}

/**
 * Copies the template into `to`, filling in `{{placeholders}}`; `_gitignore` becomes `.gitignore`.
 *
 * @param from the template directory
 * @param to the new app's directory
 * @param values the placeholders' values
 */
export function copyTemplate(from: string, to: string, values: Record<string, string>) {
  mkdirSync(to, { recursive: true })

  for (const entry of readdirSync(from)) {
    const source = join(from, entry)
    const target = join(to, entry === '_gitignore' ? '.gitignore' : entry)
    if (statSync(source).isDirectory()) {
      copyTemplate(source, target, values)
    } else {
      const text = readFileSync(source, 'utf8').replace(/\{\{(?<key>[a-z:-]+)\}\}/giu, (all, key: string) => values[key] ?? all)
      writeFileSync(target, text)
    }
  }
}

function problemWith(target: string, name: string, framework: string | null): string | null {
  const problems: [boolean, string][] = [
    [existsSync(target) && readdirSync(target).length > 0, `${target} isn't empty`],
    [!NAME.test(name), `'${name}' isn't a valid app name: use lowercase letters, digits and '-'`],
    [framework === null, 'the framework packages aren\'t published yet: pass --framework=<a pico-desktop checkout>'],
  ]

  return problems.find(([bad]) => bad)?.[1] ?? null
}

function frameworkFor(options: CreateOptions): string | null {
  if (options.framework) {
    return resolve(options.framework)
  }

  return isFramework(CHECKOUT) ? CHECKOUT : null
}

/**
 * Creates an app.
 *
 * @param options where, its ID, and the framework checkout to link
 * @param io where output goes
 * @returns the exit code
 */
export function create(options: CreateOptions, io: Output): number {
  const target = resolve(options.dir)
  const name = basename(target)
  const framework = frameworkFor(options)
  const problem = problemWith(target, name, framework)
  if (problem !== null) {
    io.err(`pico-desktop create: ${problem}`)

    return 1
  }
  const id = options.id ?? `com.example.${name}`
  copyTemplate(TEMPLATE, target, placeholders(name, id, framework!))
  generate(resolveConfig(target, { app: { id } }))
  io.out(`Created ${name} (${id}) in ${target}\n\n  cd ${options.dir}\n  pnpm install\n  pnpm dev`)

  return 0
}

