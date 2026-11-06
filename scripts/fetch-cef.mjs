#!/usr/bin/env node
// Downloads the CEF distribution pinned in toolchain.json for this machine's target, verifies it
// against the pinned SHA-1 (from cef-builds' index.json), unpacks it into a per-user cache and
// prints its path (the CEF_ROOT for CMake). Re-running is a no-op once it's unpacked.
//
// Usage: node scripts/fetch-cef.mjs [--target linux-arm64] [--print-root]
// Cache: $PD_CEF_CACHE, else $XDG_CACHE_HOME/pico-desktop/cef, else ~/.cache/pico-desktop/cef.
import { createHash } from 'node:crypto'
import { createReadStream, createWriteStream, existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs'
import { homedir } from 'node:os'
import { join } from 'node:path'
import { Readable } from 'node:stream'
import { pipeline } from 'node:stream/promises'
import { execFileSync } from 'node:child_process'

const root = new URL('..', import.meta.url).pathname
const toolchain = JSON.parse(readFileSync(join(root, 'toolchain.json'), 'utf8'))
const cdn = 'https://cef-builds.spotifycdn.com'

const hostTarget = () => {
  const os = { linux: 'linux', darwin: 'macos', win32: 'windows' }[process.platform]
  const arch = { x64: 'x64', arm64: 'arm64' }[process.arch]

  return `${os}-${arch}`
}

const argValue = (name) => {
  const i = process.argv.indexOf(name)

  return i >= 0 ? process.argv[i + 1] : undefined
}

const cacheDir = () => process.env.PD_CEF_CACHE
  ?? join(process.env.XDG_CACHE_HOME ?? join(homedir(), '.cache'), 'pico-desktop', 'cef')

const hashFile = async (path, algorithm) => {
  const hash = createHash(algorithm)
  await pipeline(createReadStream(path), hash)

  return hash.digest('hex')
}

async function download(url, to) {
  const response = await fetch(url)
  if (!response.ok || !response.body) {
    throw new Error(`download failed: ${response.status} ${response.statusText}: ${url}`)
  }
  const total = Number(response.headers.get('content-length') ?? 0)
  let received = 0
  let shown = -1
  const progress = new TransformStream({
    transform(chunk, controller) {
      received += chunk.length
      const percent = total ? Math.floor((received / total) * 100) : 0
      if (percent >= shown + 10) {
        shown = percent
        process.stderr.write(`  ${percent}% of ${Math.round(total / 1e6)} MB\n`)
      }
      controller.enqueue(chunk)
    },
  })
  await pipeline(Readable.fromWeb(response.body.pipeThrough(progress)), createWriteStream(to))
}

async function main() {
  const target = argValue('--target') ?? hostTarget()
  const archive = toolchain.cef.archives[target]
  if (!archive) {
    throw new Error(`no CEF archive pinned for ${target} in toolchain.json`)
  }
  const dir = join(cacheDir(), toolchain.cef.version, target)
  const unpacked = join(dir, archive.file.replace(/\.tar\.bz2$/, ''))
  const marker = join(dir, '.complete')
  if (!existsSync(marker)) {
    mkdirSync(dir, { recursive: true })
    const file = join(dir, archive.file)
    if (!existsSync(file)) {
      process.stderr.write(`fetching ${archive.file}\n`)
      await download(`${cdn}/${encodeURIComponent(archive.file)}`, `${file}.part`)
      renameSync(`${file}.part`, file)
    }
    const sha1 = await hashFile(file, 'sha1')
    if (sha1 !== archive.sha1) {
      rmSync(file)
      throw new Error(`SHA-1 mismatch for ${archive.file}: got ${sha1}, pinned ${archive.sha1} (the download was deleted)`)
    }
    const sha256 = await hashFile(file, 'sha256')
    process.stderr.write(`verified SHA-1 ${sha1}\nSHA-256 ${sha256}\nunpacking…\n`)
    rmSync(unpacked, { recursive: true, force: true })
    execFileSync('tar', ['-xjf', file, '-C', dir], { stdio: 'inherit' })
    rmSync(file)
    writeFileSync(marker, JSON.stringify({ version: toolchain.cef.version, target, sha1, sha256 }, null, 2) + '\n')
  }
  process.stdout.write(`${unpacked}\n`)
}

main().catch((error) => {
  process.stderr.write(`fetch-cef: ${error.message}\n`)
  process.exit(1)
})
