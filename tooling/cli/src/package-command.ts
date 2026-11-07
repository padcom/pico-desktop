// `pico-desktop package`: the bundle (built afresh) as a portable archive,
// dist/<name>-<version>-<target>.tar.gz, with its SHA-256 next to it; on Linux also an AppDir and,
// when appimagetool is available, the AppImage (appdir.ts).
import { createHash } from 'node:crypto'
import { readFileSync, writeFileSync } from 'node:fs'
import { basename, dirname, relative } from 'node:path'
import { appImagePath, makeAppDir } from './appdir'
import { type Bundle, buildBundle, reportingRuntimeErrors } from './build'
import type { ResolvedConfig } from './config'
import type { Output } from './io'
import { capture } from './processes'

function sha256File(file: string) {
  const hash = createHash('sha256').update(readFileSync(file)).digest('hex')
  writeFileSync(`${file}.sha256`, `${hash}  ${basename(file)}\n`)
}

/**
 * The AppDir, and the AppImage when appimagetool is there (it reports which).
 *
 * @param config the app
 * @param bundle the bundle
 * @param io where output goes
 * @returns false only if appimagetool ran and failed
 */
async function appImage(config: ResolvedConfig, bundle: Bundle, io: Output): Promise<boolean> {
  const appDir = makeAppDir(config, bundle)
  const tool = process.env.PICO_DESKTOP_APPIMAGETOOL ?? 'appimagetool'
  const out = appImagePath(bundle.dir)
  const arch = out.endsWith('-aarch64.AppImage') ? 'aarch64' : 'x86_64'
  const env = { ...process.env, ARCH: arch, APPIMAGE_EXTRACT_AND_RUN: '1' }
  const result = await capture(tool, ['--no-appstream', appDir, out], dirname(appDir), env)
  const outcomes: Record<string, () => boolean> = {
    127: () => {
      io.out(`made ${relative(config.root, appDir)} (install appimagetool to make the AppImage from it)`)

      return true
    },
    0: () => {
      sha256File(out)
      io.out(`packaged ${relative(config.root, out)} (+ .sha256)`)

      return true
    },
  }
  const failed = () => {
    io.err(`pico-desktop package: appimagetool failed:\n${result.output.trim()}`)

    return false
  }

  return (outcomes[result.code] ?? failed)()
}

async function archive(dir: string, io: Output): Promise<string | null> {
  const file = `${dir}.tar.gz`
  const { code, output } = await capture('tar', ['-C', dirname(dir), '-czf', file, basename(dir)], dirname(dir))
  if (code !== 0) {
    io.err(`pico-desktop package: tar failed: ${output.trim()}`)

    return null
  }
  sha256File(file)

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

    return (await appImage(config, bundle!, io)) ? 0 : 1
  })
}
