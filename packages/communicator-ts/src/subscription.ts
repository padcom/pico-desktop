// Managed subscriptions: a stable object for the caller, whatever happens to the backend. After a
// restart the Communicator subscribes again (missed values are never replayed).
import type { CborValue } from '@pico-desktop/protocol'
import type { CommunicatorError } from './errors'

export interface Delivery {
  /** Values dropped by the publisher just before this one (it fell behind). */
  dropped: number
}

export type ValueListener = (value: any, delivery: Delivery) => void

export interface SubscriptionSetup {
  topic: string
  params: CborValue | undefined
  onValue: ValueListener
  onEnd?: ((error: CommunicatorError | null) => void) | undefined
  /** How the Communicator stops it (sends `unsubscribe`). */
  cancel: (subscription: Subscription) => Promise<void>
}

export class Subscription {
  /** The publisher's ID for the current epoch (none while (re)subscribing). */
  current: number | null = null
  readonly topic: string
  readonly params: CborValue | undefined
  private ended = false

  constructor(private readonly setup: SubscriptionSetup) {
    this.topic = setup.topic
    this.params = setup.params
  }

  /**
   * Whether values may still arrive.
   *
   * @returns true until it's unsubscribed or ended
   */
  get active(): boolean {
    return !this.ended
  }

  /**
   * Stops the subscription.
   *
   * @returns once the request to stop has been sent
   */
  unsubscribe(): Promise<void> {
    if (this.ended) {
      return Promise.resolve()
    }
    this.ended = true

    return this.setup.cancel(this)
  }

  deliver(value: CborValue | undefined, dropped: number) {
    if (!this.ended) {
      this.setup.onValue(value, { dropped })
    }
  }

  /**
   * The publisher ended it (or refused it).
   *
   * @param error why, if it failed
   */
  end(error: CommunicatorError | null) {
    if (!this.ended) {
      this.ended = true
      this.setup.onEnd?.(error)
    }
  }
}
