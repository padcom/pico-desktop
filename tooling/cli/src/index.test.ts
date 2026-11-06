import { describe, it, expect } from 'vitest'
import pkg from '../package.json' with { type: 'json' }
import { COMMANDS, main, type Output } from '.'

async function run(...args: string[]) {
  const out: string[] = []
  const err: string[] = []
  const io: Output = { out: t => out.push(t), err: t => err.push(t) }
  const code = await main(args, io)

  return { code, out: out.join('\n'), err: err.join('\n') }
}

describe('cli', () => {
  it('prints its version', async () => {
    expect(await run('--version')).toEqual({ code: 0, out: `pico-desktop ${pkg.version}`, err: '' })
  })

  it('prints help with every command', async () => {
    const { code, out } = await run('--help')
    expect(code).toBe(0)

    for (const command of COMMANDS) {
      expect(out).toContain(`  ${command}`)
    }
  })

  it('prints usage and fails without a command', async () => {
    expect((await run()).code).toBe(1)
  })

  it('rejects unknown commands', async () => {
    const { code, err } = await run('deploy')
    expect(code).toBe(1)
    expect(err).toContain("unknown command 'deploy'")
  })
})
