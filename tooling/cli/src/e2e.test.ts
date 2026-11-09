import { spawn } from 'node:child_process'
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterAll, describe, expect, it } from 'vitest'
import { E2eApp, E2eError } from './e2e'

const scratch = mkdtempSync(join(tmpdir(), 'pd-e2e-'))

afterAll(() => rmSync(scratch, { recursive: true, force: true }))

/** The first bytes of every PNG. */
const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])

/** An app whose page answers DevTools commands with `answer`, and records them. */
class FakePage extends E2eApp {
  readonly asked: { method: string, params: Record<string, unknown> }[] = []

  constructor(private readonly answer: unknown) {
    super(spawn(process.execPath, ['-e', ''], { stdio: ['pipe', 'pipe', 'pipe'] }))
  }

  override devtools(method: string, params: Record<string, unknown> = {}): Promise<unknown> {
    this.asked.push({ method, params })

    return Promise.resolve(this.answer)
  }
}

describe('screenshot', () => {
  it('asks the page for a PNG and saves what it sends, making the folders on the way', async () => {
    const png = Buffer.concat([PNG_SIGNATURE, Buffer.from('the rest of a picture')])
    const page = new FakePage({ data: png.toString('base64') })
    const path = join(scratch, 'shots', '2026', 'flight-data.png')

    await page.screenshot(path)

    expect(page.asked).toEqual([{ method: 'Page.captureScreenshot', params: { format: 'png' } }])
    expect(readFileSync(path).subarray(0, 8)).toEqual(PNG_SIGNATURE)
    expect(readFileSync(path)).toEqual(png)
  })

  it('says so, and writes nothing, when the page gives no picture', async () => {
    const page = new FakePage({})
    const path = join(scratch, 'nothing', 'none.png')

    await expect(page.screenshot(path)).rejects.toThrow(E2eError)
    expect(existsSync(path)).toBe(false)
  })
})
