import { describe, expect, it } from 'vitest'
import { Rebuilder } from './rebuilder'

const sleep = (ms: number) => new Promise(resolve => {
  setTimeout(resolve, ms)
})

function rig(results: boolean[], buildMs = 0) {
  const log: string[] = []
  const rebuilder = new Rebuilder({
    build: async () => {
      log.push('build')
      await sleep(buildMs)

      return results.shift() ?? true
    },
    built: () => log.push('built'),
    delay: 5,
  })

  return { log, rebuilder }
}

describe('Rebuilder', () => {
  it('makes one build of a burst of changes', async () => {
    const { log, rebuilder } = rig([true])
    rebuilder.changed()
    rebuilder.changed()
    rebuilder.changed()
    await sleep(20)
    await rebuilder.idle()
    expect(log).toEqual(['build', 'built'])
  })

  it('doesn\'t report a failed build', async () => {
    const { log, rebuilder } = rig([false])
    rebuilder.changed()
    await sleep(20)
    await rebuilder.idle()
    expect(log).toEqual(['build'])
  })

  it('builds once more after changes during a build, never two at once', async () => {
    const { log, rebuilder } = rig([true, true], 30)
    rebuilder.changed()
    await sleep(15)
    rebuilder.changed()
    rebuilder.changed()
    await sleep(80)
    await rebuilder.idle()
    expect(log).toEqual(['build', 'built', 'build', 'built'])
  })
})
