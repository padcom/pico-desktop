// The TypeScript Communicator (PROJECT_DESCRIPTION.md §5, docs/protocol.md): typed string-named
// RPC both ways, events and managed subscriptions over any Transport. Connection state follows
// the host's lifecycle messages; nothing is ever retried or replayed.
import {
  type CborValue,
  type Envelope,
  type ErrorInfo,
  PROTOCOL_VERSION,
  decodeEnvelope,
  encodeEnvelope,
} from '@pico-desktop/protocol'
import { CommunicatorError } from './errors'
import { PendingRequests } from './pending'
import type {
  AnyServices,
  EventName,
  MethodName,
  ParamsOf,
  PayloadOf,
  RequestOf,
  ResponseOf,
  TopicName,
  ValueOf,
} from './services'
import { Subscription, type ValueListener } from './subscription'
import type { Transport } from './transport'

export const CONNECTION_STATES = ['connecting', 'ready', 'disconnected', 'restarting', 'failed', 'closing'] as const

export type ConnectionState = typeof CONNECTION_STATES[number]

export type Part = 'request' | 'response' | 'payload' | 'params' | 'value'

/** Checks an incoming payload against the contract: null when it's fine, else the problem. */
export type Validate = (name: string, part: Part, value: CborValue | undefined) => string | null

export interface CommunicatorOptions {
  transport: Transport
  /** Default request timeout in ms (30 s). */
  timeout?: number
  /** Requests waiting at once before new ones fail with BUSY (1024). */
  maxPending?: number
  validate?: Validate
}

export interface RequestOptions {
  timeout?: number
  signal?: AbortSignal
}

export interface RequestContext {
  /** Aborted when the caller cancels or the connection goes away. */
  signal: AbortSignal
}

export type Handler = (payload: any, context: RequestContext) => unknown

type StateListener = (state: ConnectionState, epoch: number) => void

type Outgoing = Omit<Envelope, 'v'>

const LIFECYCLE_STATES = new Set<string>(CONNECTION_STATES)

const withPayload = (payload: unknown) => (payload === undefined ? {} : { p: payload as CborValue })

const withData = (data: CborValue | undefined) => (data === undefined ? {} : { data })

const toError = (err: ErrorInfo) => new CommunicatorError(err.code, err.message, err.data)

function errorFrom(error: unknown, signal: AbortSignal): ErrorInfo {
  if (error instanceof CommunicatorError) {
    return { code: error.code, message: error.message, ...withData(error.data) }
  }
  const message = error instanceof Error ? error.message : String(error)

  return { code: signal.aborted ? 'CANCELLED' : 'HANDLER_ERROR', message }
}

export class Communicator<S = AnyServices> {
  private stateValue: ConnectionState = 'connecting'
  private epochValue = 0
  private nextId = 1
  private readonly outbox: Outgoing[] = []
  private readonly pending = new PendingRequests()
  private readonly handlers = new Map<string, Handler>()
  private readonly listeners = new Map<string, Set<(payload: any) => void>>()
  private readonly incoming = new Map<number, AbortController>()
  private readonly stateListeners = new Set<StateListener>()
  private readonly subscriptions = new Set<Subscription>()
  private readonly bySubscriptionId = new Map<number, Subscription>()
  private readonly transport: Transport
  private readonly timeout: number
  private readonly maxPending: number
  private readonly validate: Validate

  constructor(options: CommunicatorOptions) {
    this.transport = options.transport
    this.timeout = options.timeout ?? 30_000
    this.maxPending = options.maxPending ?? 1024
    this.validate = options.validate ?? (() => null)
    this.transport.connect(message => this.receive(message))
    this.send({ k: 'lifecycle', n: 'hello', p: { protocol: [PROTOCOL_VERSION, PROTOCOL_VERSION], role: 'frontend' } })
  }

  /**
   * The connection's state.
   *
   * @returns connecting, ready, disconnected, restarting, failed or closing
   */
  get state(): ConnectionState {
    return this.stateValue
  }

  /**
   * The backend's epoch.
   *
   * @returns 1, 2, … per backend start (0 before the first)
   */
  get epoch(): number {
    return this.epochValue
  }

  /**
   * Calls a method on the other side.
   *
   * @param name the method
   * @param payload its request
   * @param options a timeout and an AbortSignal
   * @returns its response
   */
  async request<K extends MethodName<S>>(
    name: K,
    payload: RequestOf<S, K>,
    options: RequestOptions = {},
  ): Promise<ResponseOf<S, K>> {
    const answer = await this.call('request', name, payload, options)

    return answer.p as ResponseOf<S, K>
  }

  /**
   * Answers the other side's calls to `name` (only registered names are callable).
   *
   * @param name the method
   * @param handler returns (or resolves to) the response; throwing a CommunicatorError sends its code
   * @returns a function that unregisters it
   */
  handle<K extends MethodName<S>>(name: K, handler: (payload: RequestOf<S, K>, context: RequestContext) => unknown): () => void {
    this.handlers.set(name, handler as Handler)

    return () => this.handlers.delete(name)
  }

  /**
   * Sends a one-way event (queued until the backend is ready).
   *
   * @param name the event
   * @param payload its payload
   */
  emit<K extends EventName<S>>(name: K, payload: PayloadOf<S, K>) {
    this.queue({ k: 'event', n: name, ...withPayload(payload) })
  }

  /**
   * Listens to an event.
   *
   * @param name the event
   * @param listener called with each payload
   * @returns a function that stops listening
   */
  on<K extends EventName<S>>(name: K, listener: (payload: PayloadOf<S, K>) => void): () => void {
    const set = this.listeners.get(name) ?? new Set()
    set.add(listener)
    this.listeners.set(name, set)

    return () => set.delete(listener)
  }

  /**
   * Subscribes to a topic. The subscription survives backend restarts: the Communicator
   * subscribes again once the backend is ready (missed values aren't replayed).
   *
   * @param name the topic
   * @param params its parameters
   * @param onValue called with each value
   * @param onEnd called once if the publisher ends it (with the error, if it failed)
   * @returns the subscription, once the publisher accepted it
   */
  async subscribe<K extends TopicName<S>>(
    name: K,
    params: ParamsOf<S, K>,
    onValue: (value: ValueOf<S, K>, delivery: { dropped: number }) => void,
    onEnd?: (error: CommunicatorError | null) => void,
  ): Promise<Subscription> {
    const subscription = new Subscription({
      topic: name,
      params: params as CborValue,
      onValue: onValue as ValueListener,
      onEnd,
      cancel: s => this.cancelSubscription(s),
    })
    this.attach(subscription, await this.accepted(subscription))
    this.subscriptions.add(subscription)

    return subscription
  }

  /**
   * Observes the connection.
   *
   * @param listener called with the new state and epoch on every change
   * @returns a function that stops observing
   */
  onConnectionChange(listener: StateListener): () => void {
    this.stateListeners.add(listener)

    return () => this.stateListeners.delete(listener)
  }

  // ------------------------------------------------------------------------------- sending

  private send(envelope: Outgoing) {
    this.transport.send(encodeEnvelope(envelope))
  }

  private queue(envelope: Outgoing) {
    if (this.stateValue === 'ready') {
      this.send(envelope)
    } else {
      this.outbox.push(envelope)
    }
  }

  private flush() {
    for (const envelope of this.outbox.splice(0)) {
      this.send(envelope)

      if (envelope.id !== undefined) {
        this.pending.markSent(envelope.id)
      }
    }
  }

  private refuseEarly(name: string, options: RequestOptions): CommunicatorError | null {
    if (this.pending.size >= this.maxPending) {
      return new CommunicatorError('BUSY', `${this.maxPending} requests are already waiting`)
    }

    return options.signal?.aborted ? new CommunicatorError('CANCELLED', `${name} was cancelled before it was sent`) : null
  }

  private call(kind: 'request' | 'subscribe', name: string, payload: unknown, options: RequestOptions): Promise<Envelope> {
    const refused = this.refuseEarly(name, options)
    if (refused) {
      return Promise.reject(refused)
    }
    const id = this.nextId++

    return new Promise((resolve, reject) => {
      this.pending.add(id, { name, sent: false, resolve, reject, cleanup: this.watch(id, name, options) })
      this.queue({ k: kind, id, n: name, ...withPayload(payload) })

      if (this.stateValue === 'ready') {
        this.pending.markSent(id)
      }
    })
  }

  /**
   * Arms the timeout and the abort signal of request `id`.
   *
   * @param id the request
   * @param name its method
   * @param options its timeout and signal
   * @returns what undoes them once the request settles
   */
  private watch(id: number, name: string, options: RequestOptions): () => void {
    const giveUp = (code: string, message: string) => {
      if (this.pending.wasSent(id)) {
        this.send({ k: 'cancel', id })
      }
      this.pending.reject(id, new CommunicatorError(code, message))
    }
    const timer = setTimeout(() => giveUp('TIMEOUT', `${name} didn't answer in time`), options.timeout ?? this.timeout)
    const onAbort = () => giveUp('CANCELLED', `${name} was cancelled`)
    options.signal?.addEventListener('abort', onAbort, { once: true })

    return () => {
      clearTimeout(timer)
      options.signal?.removeEventListener('abort', onAbort)
    }
  }

  // ----------------------------------------------------------------------------- receiving

  private readonly dispatch: Record<Envelope['k'], (e: Envelope) => void> = {
    request: e => void this.answer(e),
    response: e => this.onResponse(e),
    error: e => this.pending.reject(e.id!, toError(e.err!)),
    event: e => this.onEvent(e),
    subscribe: e => this.send({ k: 'error', id: e.id!, err: { code: 'UNKNOWN_METHOD', message: `no topic ${e.n} here` } }),
    unsubscribe: e => this.onPublisherEnd(e),
    cancel: e => this.incoming.get(e.id!)?.abort(),
    lifecycle: e => this.onLifecycle(e),
  }

  private receive(message: Uint8Array) {
    let envelope: Envelope
    try {
      envelope = decodeEnvelope(message)
    } catch (error) {
      console.warn('pico-desktop: dropped an invalid message', error)

      return
    }
    this.dispatch[envelope.k](envelope)
  }

  private onResponse(e: Envelope) {
    const name = this.pending.name(e.id!)
    const problem = name === undefined ? null : this.validate(name, 'response', e.p)
    if (problem) {
      this.pending.reject(e.id!, new CommunicatorError('INVALID_PAYLOAD', problem))
    } else {
      this.pending.resolve(e.id!, e)
    }
  }

  /**
   * Why an incoming request can't be handled, if it can't.
   *
   * @param e the request
   * @returns the error to send, or null when its handler should run
   */
  private refusal(e: Envelope): ErrorInfo | null {
    if (!this.handlers.has(e.n!)) {
      return { code: 'UNKNOWN_METHOD', message: `no handler for ${e.n}` }
    }
    const problem = this.validate(e.n!, 'request', e.p)

    return problem ? { code: 'INVALID_PAYLOAD', message: problem } : null
  }

  private async answer(e: Envelope) {
    const refused = this.refusal(e)
    if (refused) {
      this.send({ k: 'error', id: e.id!, err: refused })

      return
    }
    const controller = new AbortController()
    this.incoming.set(e.id!, controller)
    const reply = await this.run(this.handlers.get(e.n!)!, e, controller.signal)
    this.incoming.delete(e.id!)
    this.send(reply)
  }

  private async run(handler: Handler, e: Envelope, signal: AbortSignal): Promise<Outgoing> {
    try {
      return { k: 'response', id: e.id!, ...withPayload(await handler(e.p, { signal })) }
    } catch (error) {
      return { k: 'error', id: e.id!, err: errorFrom(error, signal) }
    }
  }

  private onEvent(e: Envelope) {
    if (e.s !== undefined) {
      this.onSubscriptionValue(e)
    } else if (this.validate(e.n!, 'payload', e.p) === null) {
      this.notify(e.n!, e.p)
    }
  }

  private notify(name: string, payload: CborValue | undefined) {
    for (const listener of this.listeners.get(name) ?? []) {
      listener(payload)
    }
  }

  // ------------------------------------------------------------------------- subscriptions

  /**
   * Asks the publisher for the subscription.
   *
   * @param subscription the subscription
   * @returns the publisher's ID for it
   */
  private async accepted(subscription: Subscription): Promise<number> {
    const answer = await this.call('subscribe', subscription.topic, subscription.params, {})
    if (answer.s === undefined) {
      throw new CommunicatorError('INVALID_MESSAGE', `the publisher of ${subscription.topic} accepted without a subscription id`)
    }

    return answer.s
  }

  private attach(subscription: Subscription, s: number) {
    if (!subscription.active) {
      // Unsubscribed while the publisher was answering: let it go again.
      this.send({ k: 'unsubscribe', s })

      return
    }
    subscription.current = s
    this.bySubscriptionId.set(s, subscription)
  }

  private onSubscriptionValue(e: Envelope) {
    const subscription = this.bySubscriptionId.get(e.s!)
    if (subscription && this.validate(subscription.topic, 'value', e.p) === null) {
      subscription.deliver(e.p, e.d ?? 0)
    }
  }

  private onPublisherEnd(e: Envelope) {
    const subscription = this.bySubscriptionId.get(e.s!)
    if (subscription) {
      this.forget(subscription)
      subscription.end(e.err ? toError(e.err) : null)
    }
  }

  private cancelSubscription(subscription: Subscription): Promise<void> {
    const s = subscription.current
    this.forget(subscription)

    if (s !== null && this.stateValue === 'ready') {
      this.send({ k: 'unsubscribe', s })
    }

    return Promise.resolve()
  }

  private forget(subscription: Subscription) {
    this.subscriptions.delete(subscription)
    this.detach(subscription)
  }

  private detach(subscription: Subscription) {
    if (subscription.current !== null) {
      this.bySubscriptionId.delete(subscription.current)
      subscription.current = null
    }
  }

  private async reactivate(subscription: Subscription) {
    try {
      this.attach(subscription, await this.accepted(subscription))
    } catch (error) {
      this.forget(subscription)
      subscription.end(error instanceof CommunicatorError ? error : new CommunicatorError('INTERNAL', String(error)))
    }
  }

  // ----------------------------------------------------------------------------- lifecycle

  private onLifecycle(e: Envelope) {
    if (LIFECYCLE_STATES.has(e.n!)) {
      this.changeState(e.n as ConnectionState, e.e)

      for (const listener of this.stateListeners) {
        listener(this.stateValue, this.epochValue)
      }
    }
  }

  private changeState(state: ConnectionState, epoch: number | undefined) {
    if (state === 'ready') {
      this.becomeReady(epoch ?? this.epochValue + 1)
    } else {
      this.lose(state)
    }
  }

  private becomeReady(epoch: number) {
    const restarted = this.epochValue !== 0 && epoch !== this.epochValue
    this.stateValue = 'ready'
    this.epochValue = epoch
    this.flush()

    if (restarted) {
      for (const subscription of this.subscriptions) {
        void this.reactivate(subscription)
      }
    }
  }

  /**
   * The backend link is down (or closing): everything waiting fails, nothing is replayed.
   *
   * @param state the new state
   */
  private lose(state: ConnectionState) {
    this.stateValue = state
    this.outbox.splice(0)
    this.pending.rejectAll('DISCONNECTED', `the backend is ${state}`)

    for (const controller of this.incoming.values()) {
      controller.abort()
    }
    this.incoming.clear()

    for (const subscription of this.subscriptions) {
      this.detach(subscription)
    }
  }
}
