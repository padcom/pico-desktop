/**
 * The TypeScript side of the Communicator: typed string-keyed RPC, events and managed
 * subscriptions to the Pico backend (implemented in F3; see docs/architecture.md).
 */
export { PROTOCOL_VERSION } from '@pico-desktop/protocol'

/** Observable connection states (PROJECT_DESCRIPTION.md §5; names are frozen with the protocol in F2). */
export const CONNECTION_STATES = [
  'connecting',
  'ready',
  'disconnected',
  'restarting',
  'failed',
  'closing',
] as const

export type ConnectionState = typeof CONNECTION_STATES[number]
