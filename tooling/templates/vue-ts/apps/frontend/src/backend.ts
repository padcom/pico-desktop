// The app's logic as a composable, without any markup, so it's testable with a fake backend.
import { Communicator, CommunicatorError, type ConnectionState, type Transport } from '@pico-desktop/communicator'
import { onScopeDispose, ref } from 'vue'
import { type Services, validate } from './generated/app'

export function useBackend(transport: Transport) {
  const communicator = new Communicator<Services>({ transport, validate })
  const state = ref<ConnectionState>('connecting')
  const message = ref('')
  const tick = ref<number | undefined>()

  communicator.onConnectionChange(next => {
    state.value = next
  })

  async function greet(name: string) {
    try {
      message.value = (await communicator.request('greeting.hello', { name })).message
    } catch (error) {
      message.value = error instanceof CommunicatorError ? `${error.code}: ${error.message}` : String(error)
    }
  }

  // Register the cleanup now: Vue has no active scope after an `await`. If the subscription
  // resolves after the scope is gone, it is dropped at once.
  let disposed = false
  let subscription: { unsubscribe(): void } | undefined
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

  return { state, message, tick, greet }
}
