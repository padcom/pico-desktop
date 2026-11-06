// `pico-desktop package`: the bundle (built afresh) as a portable archive,
// dist/<name>-<version>-<target>.tar.gz, with its SHA-256 next to it.
import { createHash } from 'node:crypto'
import { readFileSync, writeFileSync } from 'node:fs'
import { basename, dirname, relative } from 'node:path'
import { buildBundle, reportingRuntimeErrors } from './build'
import type { ResolvedConfig } from './config'
import type { Output } from './io'
import { capture } from './processes'

async function archive(dir: string, io: Output): Promise<string | null> {
  const file = `${dir}.tar.gz`
  const { code, output } = await capture('tar', ['-C', dirname(dir), '-czf', file, basename(dir)], dirname(dir))
  if (code !== 0) {
    io.err(`pico-desktop package: tar failed: ${output.trim()}`)

    return null
  }
  const hash = createHash('sha256').update(readFileSync(file)).digest('hex')
  writeFileSync(`${file}.sha256`, `${hash}  ${basename(file)}\n`)

  return file
}

/**
 * `pico-desktop package`.
 *
 * @param config the app
 * @param io where output goes
 * @returns the exit code
 */
export function packageApp(config: ResolvedConfig, io: Output): Promise<number> {
  return reportingRuntimeErrors('package', io, async () => {
    const bundle = await buildBundle(config, io)
    const file = bundle ? await archive(bundle.dir, io) : null
    if (!file) {
      return 1
    }
    io.out(`packaged ${relative(config.root, file)} (+ .sha256)`)

    return 0
  })
}
