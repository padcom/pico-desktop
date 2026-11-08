// The app's logic as a composable, without any markup, so it's testable with a fake backend.
import { Communicator, CommunicatorError, type ConnectionState, type Transport } from '@pico-desktop/communicator'
import { type Ref, onScopeDispose, ref, toValue } from 'vue'
import { type Services, validate } from './generated/app'

type Subscription = { unsubscribe(): void }

/**
 * Subscribes to the backend's clock, and ends the subscription with the current scope.
 *
 * @param communicator the link to the backend
 * @param tick the latest tick
 * @param message where a failure is reported
 */
function useClock(communicator: Communicator<Services>, tick: Ref<number | undefined>, message: Ref<string>) {
  // Register the cleanup now: Vue has no active scope after an `await`. If the subscription
  // resolves after the scope is gone, it is dropped at once.
  let disposed = false
  let subscription: Subscription | undefined
  onScopeDispose(() => {
    disposed = true
    subscription?.unsubscribe()
  })

  void (async () => {
    try {
      const subscribed = await communicator.subscribe('clock.ticks', { intervalMs: 1000 }, ({ n }) => {
        tick.value = n
      })
      if (disposed) {
        subscribed.unsubscribe()
      } else {
        subscription = subscribed
      }
    } catch (error) {
      message.value = `the clock: ${String(error)}`
    }
  })()
}

/**
 * Says hello through the backend.
 *
 * @param communicator the link to the backend
 * @param name who to greet
 * @returns the greeting, or the error's description
 */
async function greeting(communicator: Communicator<Services>, name: string): Promise<string> {
  try {
    return (await communicator.request('greeting.hello', { name })).message
  } catch (error) {
    return error instanceof CommunicatorError ? `${error.code}: ${error.message}` : String(error)
  }
}

/**
 * The app's state and actions over a link to the backend.
 *
 * @param transport the link (read once, when the component is set up)
 * @returns the connection state, the last message and tick, and `greet`
 */
export function useBackend(transport: Transport | (() => Transport)) {
  const communicator = new Communicator<Services>({ transport: toValue(transport), validate })
  const state = ref<ConnectionState>('connecting')
  const message = ref('')
  const tick = ref<number | undefined>()

  communicator.onConnectionChange(next => {
    state.value = next
  })
  useClock(communicator, tick, message)

  return {
    state,
    message,
    tick,
    /**
     * Greets `name` through the backend.
     *
     * @param name who to greet
     */
    async greet(name: string) {
      message.value = await greeting(communicator, name)
    },
  }
}
