// `pico-desktop create <dir>`: a new app from a template (vanilla-ts, or vue-ts), its contract already
// generated. Until the packages are published, the app links them from a framework checkout
// (`--framework`, by default the checkout this CLI runs from).
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs'
import { basename, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { resolveConfig } from './config'
import { generate } from './generate'
import type { Output } from './io'

export const TEMPLATES = ['vanilla-ts', 'vue-ts'] as const
const templateDir = (name: string) => fileURLToPath(new URL(/* @vite-ignore */ `../../templates/${name}`, import.meta.url))
const CHECKOUT = fileURLToPath(new URL(/* @vite-ignore */ '../../..', import.meta.url))

export interface CreateOptions {
  dir: string
  id?: string | undefined
  framework?: string | undefined
  /** One of TEMPLATES; vanilla-ts by default. */
  template?: string | undefined
}

const NAME = /^[a-z0-9][a-z0-9-]*$/u

const isFramework = (dir: string) => existsSync(join(dir, 'packages', 'communicator-pico', 'project.json'))

/**
 * A pnpm `link:` to `to` from the package in `from`. Always forward slashes: it's written into
 * JSON, where a Windows path's backslashes are invalid escapes, and pnpm takes them everywhere.
 *
 * @param from the directory of the package.json holding the link
 * @param to what it links to
 * @param relativePath how to get from one to the other (for tests of another system's paths)
 * @returns the dependency's specifier
 */
export function linkTo(from: string, to: string, relativePath: typeof relative = relative): string {
  return `link:${relativePath(from, to).replaceAll('\\', '/')}`
}

/**
 * The framework's own versions of the tools the templates use, with fallbacks.
 *
 * @param dev the framework's devDependencies
 * @returns `version:<tool>` → version
 */
function toolVersions(dev: Record<string, string>): Record<string, string> {
  const versions = { eslint: '10.14.0', typescript: '5.9.3', vite: '8.3.6', vitest: '5.1.0' }

  return Object.fromEntries(Object.entries(versions).map(([tool, fallback]) => [`version:${tool}`, dev[tool] ?? fallback]))
}

/**
 * The template's placeholders for an app linked to a framework checkout.
 *
 * @param name the app's directory name
 * @param id the app's ID
 * @param framework the checkout
 * @param target the new app's directory: links are relative to the package.json that holds them,
 * so the app keeps working wherever the app and the checkout move together
 * @returns placeholder → value
 */
export function placeholders(name: string, id: string, framework: string, target: string): Record<string, string> {
  const root = JSON.parse(readFileSync(join(framework, 'package.json'), 'utf8')) as { devDependencies: Record<string, string> }
  const pico = JSON.parse(readFileSync(join(framework, 'packages/communicator-pico/project.json'), 'utf8')) as { version: string }
  // The CLI and communicator-pico are linked from the app's package.json, the others from
  // apps/frontend/package.json.
  const link = (path: string, from = target) => linkTo(from, join(framework, path))
  const frontend = join(target, 'apps', 'frontend')

  return {
    name,
    'appId': id,
    'appName': name,
    'dep:cli': link('tooling/cli'),
    'dep:communicator': link('packages/communicator-ts', frontend),
    'dep:communicator-pico': link('packages/communicator-pico'),
    'dep:protocol': link('packages/protocol', frontend),
    'version:communicator-pico': pico.version,
    ...toolVersions(root.devDependencies),
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

function problemWith(target: string, name: string, framework: string | null, template: string): string | null {
  const problems: [boolean, string][] = [
    [existsSync(target) && readdirSync(target).length > 0, `${target} isn't empty`],
    [!NAME.test(name), `'${name}' isn't a valid app name: use lowercase letters, digits and '-'`],
    [!(TEMPLATES as readonly string[]).includes(template), `'${template}' isn't a template: use ${TEMPLATES.join(' or ')}`],
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
  const template = options.template ?? 'vanilla-ts'
  const problem = problemWith(target, name, framework, template)
  if (problem !== null) {
    io.err(`pico-desktop create: ${problem}`)

    return 1
  }
  const id = options.id ?? `com.example.${name}`
  copyTemplate(templateDir(template), target, placeholders(name, id, framework!, target))
  generate(resolveConfig(target, { app: { id } }))
  io.out(`Created ${name} (${id}) in ${target}\n\n  cd ${options.dir}\n  pnpm install\n  pnpm dev`)

  return 0
}

