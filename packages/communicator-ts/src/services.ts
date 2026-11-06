// Typing the Communicator by a contract: the `Services` interface the schema tooling generates
// (ADR-0005) maps each name to a method, an event or a subscription.

export interface MethodSpec { kind: 'method', request: unknown, response: unknown }

export interface EventSpec { kind: 'event', payload: unknown }

export interface SubscriptionSpec { kind: 'subscription', params: unknown, value: unknown }

export type ServiceMap = Record<string, MethodSpec | EventSpec | SubscriptionSpec>

/** Untyped use: any name, any payload. */
export type AnyServices = Record<string, any>

type NamesOf<S, Kind> = { [K in keyof S & string]: S[K] extends { kind: Kind } ? K : never }[keyof S & string]

export type MethodName<S> = S extends ServiceMap ? NamesOf<S, 'method'> : string

export type EventName<S> = S extends ServiceMap ? NamesOf<S, 'event'> : string

export type TopicName<S> = S extends ServiceMap ? NamesOf<S, 'subscription'> : string

export type RequestOf<S, K> = K extends keyof S ? (S[K] extends MethodSpec ? S[K]['request'] : never) : any

export type ResponseOf<S, K> = K extends keyof S ? (S[K] extends MethodSpec ? S[K]['response'] : never) : any

export type PayloadOf<S, K> = K extends keyof S ? (S[K] extends EventSpec ? S[K]['payload'] : never) : any

export type ParamsOf<S, K> = K extends keyof S ? (S[K] extends SubscriptionSpec ? S[K]['params'] : never) : any

export type ValueOf<S, K> = K extends keyof S ? (S[K] extends SubscriptionSpec ? S[K]['value'] : never) : any
