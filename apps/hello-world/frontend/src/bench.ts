// The benchmark's loads, numbers and pass/fail rule (docs/ipc-benchmark.md). The self-test drives the
// real app: the Pico backend publishes each load (`bench.events`), the page records when every event
// arrives and what the publisher says it dropped.

export interface Load {
  name: string
  perSecond: number
  count: number
  bytes: number
}

/** The two loads the parity work will produce: map tiles (about 30 KB, 50 a second) and telemetry (100 a second). */
export const LOADS: Load[] = [
  { name: 'map tiles', perSecond: 50, count: 150, bytes: 30_000 },
  { name: 'telemetry', perSecond: 100, count: 300, bytes: 64 },
]

/**
 * What a healthy run may do. Deliberately generous: CI runners differ and a flaky gate costs more than
 * it catches. A real regression (events queueing up, a stalled bridge) is far outside these.
 */
export const LIMITS = { lostFraction: 0.02, p95Ms: 500, maxMs: 2000 }

export interface Sample {
  sentAt: number
  receivedAt: number
  /** What the publisher says it dropped just before this event. */
  dropped: number
}

export interface Summary {
  load: Load
  received: number
  lost: number
  reportedDropped: number
  p50: number
  p95: number
  p99: number
  max: number
  achievedPerSecond: number
}

const percentile = (sorted: number[], q: number) => sorted[Math.floor(q * (sorted.length - 1))] ?? 0

/**
 * How many events a second arrived, between the first and the last.
 *
 * @param samples what arrived, in order
 * @returns the rate, or 0 for fewer than two
 */
function arrivalRate(samples: Sample[]): number {
  if (samples.length < 2) {
    return 0
  }
  const seconds = (samples.at(-1)!.receivedAt - samples[0]!.receivedAt) / 1000

  return seconds > 0 ? (samples.length - 1) / seconds : 0
}

/**
 * The numbers of one run.
 *
 * @param load what was published
 * @param samples what arrived, in order
 * @returns latencies (ms, never negative), what was lost, what the publisher reported dropping
 */
export function summarize(load: Load, samples: Sample[]): Summary {
  const latencies = samples.map(s => Math.max(0, s.receivedAt - s.sentAt)).sort((a, b) => a - b)

  return {
    load,
    received: samples.length,
    lost: Math.max(0, load.count - samples.length),
    reportedDropped: samples.reduce((sum, s) => sum + s.dropped, 0),
    p50: percentile(latencies, 0.5),
    p95: percentile(latencies, 0.95),
    p99: percentile(latencies, 0.99),
    max: percentile(latencies, 1),
    achievedPerSecond: arrivalRate(samples),
  }
}

/**
 * One line with every number of a run.
 *
 * @param s the summary
 * @returns the line (a `pd-info` line, and the failure message)
 */
export function describe(s: Summary): string {
  const { load } = s
  const ms = (n: number) => n.toFixed(0)
  const latency = `latency p50 ${ms(s.p50)} / p95 ${ms(s.p95)} / p99 ${ms(s.p99)} / max ${ms(s.max)} ms`

  return `${load.name}: ${load.count} events of ${load.bytes} bytes at ${load.perSecond}/s: received ${s.received}, `
    + `lost ${s.lost}, dropped by the publisher ${s.reportedDropped}; ${latency}; arrived at ${s.achievedPerSecond.toFixed(1)}/s`
}

/**
 * Each limit with whether the run broke it.
 *
 * @param s the summary
 * @returns [broken, what the limit says]
 */
const limits = (s: Summary): [boolean, string][] => [
  [s.lost > s.load.count * LIMITS.lostFraction, `lost ${s.lost} of ${s.load.count} (limit ${LIMITS.lostFraction * 100}%)`],
  [s.p95 > LIMITS.p95Ms, `p95 latency ${s.p95.toFixed(0)} ms (limit ${LIMITS.p95Ms})`],
  [s.max > LIMITS.maxMs, `max latency ${s.max.toFixed(0)} ms (limit ${LIMITS.maxMs})`],
]

/**
 * Whether the run is within the limits.
 *
 * @param s the summary
 * @param enforce false to only report (macOS and Windows, until their first numbers are seen: the limits
 * were chosen before any CI number existed)
 * @returns true, or what broke which limit, with every number
 */
export function verdict(s: Summary, enforce = true): true | string {
  const broken = limits(s).filter(([bad]) => bad).map(([, why]) => why)

  return !enforce || broken.length === 0 || `${broken.join('; ')}. ${describe(s)}`
}
