// The AppImage's directory (an AppDir, https://docs.appimage.org): the bundle, plus AppRun (the
// launcher), a .desktop entry and an icon. `package` makes it next to the bundle and, when
// `appimagetool` is on the PATH (or $PICO_DESKTOP_APPIMAGETOOL names it), squashes it into
// <name>-<version>-<arch>.AppImage. The CLI downloads nothing for this.
import { chmodSync, copyFileSync, cpSync, existsSync, rmSync, symlinkSync, writeFileSync } from 'node:fs'
import { basename, dirname, extname, join } from 'node:path'
import type { Bundle } from './build'
import type { ResolvedConfig } from './config'

/**
 * The .desktop entry.
 *
 * @param config the app
 * @param name the launcher's and the icon's name
 * @returns its text
 */
export function desktopEntry(config: ResolvedConfig, name: string): string {
  return [
    '[Desktop Entry]',
    'Type=Application',
    `Name=${config.app.name.replaceAll('\n', ' ')}`,
    'Exec=AppRun',
    `Icon=${name}`,
    'Categories=Utility;',
    'Terminal=false',
    `X-AppImage-Version=${config.app.version}`,
    '',
  ].join('\n')
}

/**
 * A plain icon for apps without one: a rounded square with the name's first letter.
 *
 * @param config the app
 * @returns an SVG
 */
export function defaultIcon(config: ResolvedConfig): string {
  const letter = (config.app.name.trim()[0] ?? '?').toUpperCase().replace(/[<>&"']/u, '?')

  return [
    '<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">',
    '  <rect width="256" height="256" rx="48" fill="#5b4ccf"/>',
    `  <text x="128" y="170" font-family="sans-serif" font-size="140" text-anchor="middle" fill="#fff">${letter}</text>`,
    '</svg>',
    '',
  ].join('\n')
}

const givenIcon = (config: ResolvedConfig) => (config.icon !== undefined && existsSync(config.icon) ? config.icon : null)

function writeIcon(config: ResolvedConfig, dir: string, name: string): string {
  const given = givenIcon(config)
  if (!given) {
    writeFileSync(join(dir, `${name}.svg`), defaultIcon(config))

    return `${name}.svg`
  }
  const file = `${name}${extname(given) === '.png' ? '.png' : '.svg'}`
  copyFileSync(given, join(dir, file))

  return file
}

/**
 * Makes the AppDir from a built bundle.
 *
 * @param config the app (`icon` in the config: a .png or .svg; a plain one otherwise)
 * @param bundle the bundle
 * @returns the AppDir's path
 */
export function makeAppDir(config: ResolvedConfig, bundle: Bundle): string {
  const dir = `${bundle.dir}.AppDir`
  rmSync(dir, { recursive: true, force: true })
  cpSync(bundle.dir, dir, { recursive: true, verbatimSymlinks: true })
  // AppRun is the bundle's launcher (it finds the rest next to itself, so it works mounted too).
  copyFileSync(join(dir, bundle.name), join(dir, 'AppRun'))
  chmodSync(join(dir, 'AppRun'), 0o755)
  writeFileSync(join(dir, `${bundle.name}.desktop`), desktopEntry(config, bundle.name))
  symlinkSync(writeIcon(config, dir, bundle.name), join(dir, '.DirIcon'))

  return dir
}

/**
 * The AppImage's file name for a bundle (`demo-0.1.0-linux-arm64` → `demo-0.1.0-aarch64.AppImage`).
 *
 * @param bundleDir the bundle
 * @returns the path next to it
 */
export function appImagePath(bundleDir: string): string {
  const arch = basename(bundleDir).endsWith('-linux-arm64') ? 'aarch64' : 'x86_64'
  const stem = basename(bundleDir).replace(/-linux-(?:arm64|x64)$/u, '')

  return join(dirname(bundleDir), `${stem}-${arch}.AppImage`)
}
