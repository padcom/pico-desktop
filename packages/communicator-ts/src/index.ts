/**
 * The TypeScript side of the Communicator: typed string-keyed RPC, events and managed
 * subscriptions to the Pico backend (docs/protocol.md, docs/architecture.md).
 */
export { PROTOCOL_VERSION } from '@pico-desktop/protocol'

export {
  type CommunicatorOptions,
  type ConnectionState,
  type Handler,
  type Part,
  type RequestContext,
  type RequestOptions,
  type Validate,
  CONNECTION_STATES,
  Communicator,
} from './communicator'

export { type NativeBridge, bridgeTransport } from './bridge'

export { CommunicatorError } from './errors'

export type { AnyServices, EventSpec, MethodSpec, ServiceMap, SubscriptionSpec } from './services'

export { type Delivery, Subscription } from './subscription'

export { type Transport, memoryTransportPair } from './transport'
