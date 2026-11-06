import { describe, it, expect } from 'vitest'
import pkg from '../package.json' with { type: 'json' }
import { COMMANDS, main, type Output } from '.'

function run(...args: string[]) {
  const out: string[] = []
  const err: string[] = []
  const io: Output = { out: t => out.push(t), err: t => err.push(t) }
  const code = main(args, io)

  return { code, out: out.join('\n'), err: err.join('\n') }
}

describe('cli', () => {
  it('prints its version', () => {
    expect(run('--version')).toEqual({ code: 0, out: `pico-desktop ${pkg.version}`, err: '' })
  })

  it('prints help with every command', () => {
    const { code, out } = run('--help')
    expect(code).toBe(0)

    for (const command of COMMANDS) {
      expect(out).toContain(`  ${command}`)
    }
  })

  it('prints usage and fails without a command', () => {
    expect(run().code).toBe(1)
  })

  it('reports commands as not implemented yet', () => {
    const { code, err } = run('dev')
    expect(code).toBe(2)
    expect(err).toContain('not implemented yet')
  })

  it('rejects unknown commands', () => {
    const { code, err } = run('deploy')
    expect(code).toBe(1)
    expect(err).toContain("unknown command 'deploy'")
  })
})
