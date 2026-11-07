import { describe, expect, it } from 'vitest'
import { resolveConfig } from './config'
import { diagnose, doctor } from './doctor'
import type { Output } from './io'

describe('doctor', () => {
  it('reports every check, with what to do about problems', async () => {
    const config = resolveConfig('/nowhere', { app: { id: 'demo' }, host: '/nowhere/host' })
    const findings = await diagnose(config)
    expect(findings).toHaveLength(5)
    expect(findings[0]!.what).toMatch(/^Node v/u)
    const lines: string[] = []
    const io: Output = { out: t => lines.push(t), err: t => lines.push(t) }
    await doctor(config, io)
    expect(lines.join('\n')).toMatch(/[✓!✗] Node v/u)
  })
})
