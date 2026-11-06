// Payloads for the ADR-0005 experiment contract: valid and invalid ones per type. The TypeScript
// validators and the generated Pico decoders must both accept exactly the valid ones.
import type { CborValue } from '@pico-desktop/protocol'

export interface Case {
  name: string
  type: string
  value: CborValue
  valid: boolean
}

const tree = (depth: number): CborValue => ({ name: `n${depth}`, children: depth > 0 ? [tree(depth - 1), tree(depth - 1)] : [] })

const telemetry = { timeUs: 1_730_000_000_000_000n, attitude: { roll: 0.5, pitch: -0.25, yaw: 3.0 }, mode: 'auto' }

export const corpus: Case[] = [
  {
    name: 'a port with every member',
    type: 'serial-port',
    value: { path: '/dev/ttyACM0', vendorId: 0x1209, productId: 0x5740, serial: 'X1' },
    valid: true,
  },
  { name: 'a port with only its path', type: 'serial-port', value: { path: 'COM3' }, valid: true },
  { name: 'a port without a path', type: 'serial-port', value: { vendorId: 1 }, valid: false },
  { name: 'a 17-bit vendor id', type: 'serial-port', value: { path: 'x', vendorId: 0x10000 }, valid: false },
  { name: 'a negative vendor id', type: 'serial-port', value: { path: 'x', vendorId: -1 }, valid: false },
  { name: 'an unknown member', type: 'serial-port', value: { path: 'x', speed: 9600 }, valid: false },
  { name: 'a path that is bytes', type: 'serial-port', value: { path: new Uint8Array([1]) }, valid: false },
  { name: 'not a map', type: 'serial-port', value: ['x'], valid: false },
  { name: 'open with parity', type: 'open-params', value: { path: 'x', baud: 115200, parity: 'even' }, valid: true },
  { name: 'open with an unknown parity', type: 'open-params', value: { path: 'x', baud: 115200, parity: 'mark' }, valid: false },
  { name: 'open with a 33-bit baud rate', type: 'open-params', value: { path: 'x', baud: 2 ** 32 }, valid: false },
  {
    name: 'write raw bytes',
    type: 'write-params',
    value: { handle: 1, data: new Uint8Array([0xfd, 9]), packet: { kind: 'raw', data: new Uint8Array(0) } },
    valid: true,
  },
  {
    name: 'write a text packet',
    type: 'write-params',
    value: { handle: 1, data: new Uint8Array(1), packet: { kind: 'text', line: 'hi' } },
    valid: true,
  },
  { name: 'write with data as text', type: 'write-params', value: { handle: 1, data: 'fd09' }, valid: false },
  { name: 'a packet with an unknown kind', type: 'packet', value: { kind: 'binary', data: new Uint8Array(1) }, valid: false },
  { name: 'a text packet with raw data', type: 'packet', value: { kind: 'text', data: new Uint8Array(1) }, valid: false },
  { name: 'a raw packet', type: 'packet', value: { kind: 'raw', data: new Uint8Array([1, 2]) }, valid: true },
  { name: 'telemetry', type: 'telemetry', value: telemetry, valid: true },
  {
    name: 'telemetry with battery',
    type: 'telemetry',
    value: { ...telemetry, battery: { voltage: 12.4, remaining: 80 } },
    valid: true,
  },
  { name: 'telemetry with a 64-bit time', type: 'telemetry', value: { ...telemetry, timeUs: 2n ** 64n - 1n }, valid: true },
  {
    name: 'telemetry with integral floats (encoded as integers)',
    type: 'telemetry',
    value: { ...telemetry, attitude: { roll: 1, pitch: 0, yaw: -2 } },
    valid: true,
  },
  {
    name: 'telemetry with 101 % remaining',
    type: 'telemetry',
    value: { ...telemetry, battery: { voltage: 12, remaining: 101 } },
    valid: false,
  },
  { name: 'telemetry with an unknown mode', type: 'telemetry', value: { ...telemetry, mode: 'loiter' }, valid: false },
  { name: 'telemetry without attitude', type: 'telemetry', value: { timeUs: 1, mode: 'auto' }, valid: false },
  { name: 'a deep tree', type: 'tree', value: tree(6), valid: true },
  { name: 'a tree with a bad child', type: 'tree', value: { name: 'root', children: [{ name: 'leaf' }] }, valid: false },
  { name: 'port busy', type: 'port-busy', value: { port: 'COM1' }, valid: true },
]
