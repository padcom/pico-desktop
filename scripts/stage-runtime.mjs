// Stages the host runtime for this machine's target (linux-arm64, linux-x64, darwin-arm64):
// `cmake --install` of the CEF build, the Linux shared libraries stripped (libcef.so: 2.5 GB with
// debug info, 280 MB without; macOS: the app bundle pico-desktop-host.app as built, not touched),
// every file hashed into runtime.json, then archived:
//   build/runtime/pico-desktop-runtime-<version>-<target>/            (the runtime)
//   build/runtime/pico-desktop-runtime-<version>-<target>.tar.gz      (+ .sha256)
// Usage: node scripts/stage-runtime.mjs [--no-archive]
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { lstatSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { join, relative, resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const run = (program, args) => execFileSync(program, args, { cwd: root, stdio: 'inherit' })

const TARGETS = { 'linux-arm64': ['linux', 'arm64'], 'linux-x64': ['linux', 'x64'], 'darwin-arm64': ['darwin', 'arm64'] }
const target = Object.keys(TARGETS).find(t => TARGETS[t][0] === process.platform && TARGETS[t][1] === process.arch)
if (!target) {
  throw new Error(`no runtime staging for ${process.platform}-${process.arch} yet`)
}

const version = /project\(pico_desktop_runtime\s+VERSION\s+([\d.]+)/u.exec(readFileSync(join(root, 'CMakeLists.txt'), 'utf8'))[1]
const toolchain = JSON.parse(readFileSync(join(root, 'toolchain.json'), 'utf8'))
const name = `pico-desktop-runtime-${version}-${target}`
const out = join(root, 'build', 'runtime')
const dir = join(out, name)

rmSync(dir, { recursive: true, force: true })
run('cmake', ['--install', 'build/cef', '--prefix', dir, '--component', 'runtime'])

// Every file below `path`. A symbolic link to a directory is not entered (macOS frameworks have
// Versions/Current → A: its files would be listed twice); a link to a file is listed (and hashed as
// the file it points to), and the archive keeps the links as they are.
const files = path => readdirSync(path).flatMap((entry) => {
  const full = join(path, entry)
  const info = lstatSync(full)

  if (info.isSymbolicLink()) {
    return statSync(full, { throwIfNoEntry: false })?.isFile() ? [full] : []
  }

  return info.isDirectory() ? files(full) : [full]
})

// Linux only: macOS binaries are signed, and stripping one would break its signature.
for (const lib of process.platform === 'linux' ? files(dir).filter(f => /\.so(\.\d+)*$/u.test(f)) : []) {
  run('strip', ['--strip-unneeded', lib]) // the dynamic symbols stay; CEF publishes symbols separately
}

const sha256 = path => createHash('sha256').update(readFileSync(path)).digest('hex')
const manifest = {
  name: 'pico-desktop-runtime',
  version,
  target,
  host: 'pico-desktop-host',
  cef: { version: toolchain.cef.version, chromium: toolchain.cef.chromium },
  protocol: 1,
  pico: { minimum: toolchain.pico.minimum },
  files: Object.fromEntries(files(dir).sort().map(f => [relative(dir, f), sha256(f)])),
}
writeFileSync(join(dir, 'runtime.json'), `${JSON.stringify(manifest, null, 2)}\n`)

const size = Object.keys(manifest.files).reduce((sum, f) => sum + statSync(join(dir, f)).size, 0)
console.log(`staged ${name}: ${Object.keys(manifest.files).length} files, ${(size / 1e6).toFixed(0)} MB`)

if (!process.argv.includes('--no-archive')) {
  const archive = join(out, `${name}.tar.gz`)
  run('tar', ['-C', out, '-czf', archive, name])
  writeFileSync(`${archive}.sha256`, `${sha256(archive)}  ${name}.tar.gz\n`)
  console.log(`archived ${relative(root, archive)} (${(statSync(archive).size / 1e6).toFixed(0)} MB)`)
}
