// Rebuilds on file changes: changes within `delay` ms make one build, builds never overlap, and a
// change during a build makes one more build after it.

export interface RebuilderOptions {
  /** Builds; resolves whether it succeeded. */
  build: () => Promise<boolean>
  /** After each successful build. */
  built: () => void
  delay?: number
}

export class Rebuilder {
  private timer: ReturnType<typeof setTimeout> | null = null
  private running: Promise<void> | null = null
  private again = false

  constructor(private readonly options: RebuilderOptions) {}

  /** Something changed. */
  changed() {
    if (this.timer) {
      clearTimeout(this.timer)
    }
    this.timer = setTimeout(() => this.start(), this.options.delay ?? 150)
  }

  /**
   * Waits until no build is running or scheduled (tests, shutdown).
   *
   * @returns once idle
   */
  async idle(): Promise<void> {
    await this.running

    return this.running ? this.idle() : undefined
  }

  /** Stops scheduling builds. */
  stop() {
    if (this.timer) {
      clearTimeout(this.timer)
    }
    this.timer = null
  }

  private start() {
    this.timer = null

    if (this.running) {
      this.again = true

      return
    }
    this.running = this.run()
  }

  private async run() {
    if (await this.options.build()) {
      this.options.built()
    }
    this.running = null

    if (this.again) {
      this.again = false
      this.start()
    }
  }
}
