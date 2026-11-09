import { describe as suite, expect, it } from 'vitest'
import { LIMITS, LOADS, type Load, type Sample, describe, summarize, verdict } from './bench'

const tiles = LOADS[0]!

/**
 * Samples that arrive `latency` ms after they were sent, 20 ms apart.
 *
 * @param count how many
 * @param latency their latency in ms
 * @returns the samples
 */
function steady(count: number, latency: number): Sample[] {
  return Array.from({ length: count }, (_, i) => ({ sentAt: 1000 + i * 20, receivedAt: 1000 + i * 20 + latency, dropped: 0 }))
}

suite('the benchmark summary', () => {
  it('has the two loads of the issue: 30 KB tiles at 50 a second, telemetry at 100 a second', () => {
    expect(LOADS.map(l => [l.name, l.perSecond, l.bytes])).toEqual([['map tiles', 50, 30_000], ['telemetry', 100, 64]])
  })

  it('reads latencies, losses and the publisher\'s drops', () => {
    const samples = steady(150, 3)
    samples[100] = { sentAt: 3000, receivedAt: 3400, dropped: 2 }
    const s = summarize(tiles, samples)
    expect(s.received).toBe(150)
    expect(s.lost).toBe(0)
    expect(s.reportedDropped).toBe(2)
    expect(s.p50).toBe(3)
    expect(s.max).toBe(400)
    expect(s.achievedPerSecond).toBeCloseTo(50, 0)
  })

  it('counts what never arrived as lost', () => {
    expect(summarize(tiles, steady(120, 3)).lost).toBe(30)
    expect(summarize(tiles, []).lost).toBe(150)
  })

  it('never reports a negative latency (the two clocks have a millisecond between them)', () => {
    expect(summarize(tiles, [{ sentAt: 1000, receivedAt: 999, dropped: 0 }]).max).toBe(0)
  })
})

suite('the benchmark verdict', () => {
  it('passes a healthy run', () => {
    expect(verdict(summarize(tiles, steady(150, 5)))).toBe(true)
  })

  it('fails a run that lost more than the limit, and says all the numbers', () => {
    const result = verdict(summarize(tiles, steady(100, 5)))
    expect(result).toContain('lost 50 of 150')
    expect(result).toContain('map tiles: 150 events of 30000 bytes at 50/s: received 100, lost 50')
  })

  it('tolerates a few lost events', () => {
    const few = Math.floor(tiles.count * LIMITS.lostFraction)
    expect(verdict(summarize(tiles, steady(tiles.count - few, 5)))).toBe(true)
  })

  it('fails a run whose events queued up', () => {
    const slow = verdict(summarize(tiles, steady(150, LIMITS.p95Ms + 100)))
    expect(slow).toContain('p95 latency')
    const stalled: Load = { ...tiles }
    expect(verdict(summarize(stalled, [...steady(149, 5), { sentAt: 5000, receivedAt: 5000 + LIMITS.maxMs + 1, dropped: 0 }])))
      .toContain('max latency')
  })
})

suite('the benchmark outside Linux', () => {
  it('only reports, and passes, when the limits are not enforced', () => {
    expect(verdict(summarize(tiles, steady(100, 5)), false)).toBe(true)
    expect(verdict(summarize(tiles, steady(100, 5)), true)).not.toBe(true)
  })

  it('describes a run on one line', () => {
    const line = describe(summarize(tiles, steady(150, 4)))
    expect(line).toContain('map tiles: 150 events of 30000 bytes at 50/s: received 150, lost 0, dropped by the publisher 0;')
    expect(line).toContain('latency p50 4 / p95 4 / p99 4 / max 4 ms; arrived at 50.')
    expect(line).not.toContain('\n')
  })
})
