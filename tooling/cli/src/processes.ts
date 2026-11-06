// Running the tools the CLI drives (pico, the host, test runners), with their output prefixed.
import { spawn } from 'node:child_process'
import type { Output } from './io'

export interface Finished {
  code: number
  output: string
}

/**
 * Calls `onLine` with each complete line of a stream.
 *
 * @param stream the stream
 * @param onLine called per line (without the newline)
 */
export function lines(stream: NodeJS.ReadableStream, onLine: (line: string) => void) {
  let pending = ''
  stream.on('data', (chunk: Buffer | string) => {
    const parts = (pending + chunk.toString()).split('\n')
    pending = parts.pop() ?? ''
    parts.forEach(line => onLine(line.replace(/\r$/u, '')))
  })
  stream.on('end', () => {
    if (pending) {
      onLine(pending)
    }
  })
}

/**
 * Runs a program to the end and collects what it printed.
 *
 * @param program the executable
 * @param args its arguments
 * @param cwd where it runs
 * @returns its exit code (127 if it couldn't start) and its output
 */
export function capture(program: string, args: string[], cwd: string): Promise<Finished> {
  return new Promise(resolve => {
    const chunks: string[] = []
    const child = spawn(program, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
    child.stdout.on('data', (b: Buffer) => chunks.push(b.toString()))
    child.stderr.on('data', (b: Buffer) => chunks.push(b.toString()))
    child.on('error', error => resolve({ code: 127, output: String(error) }))
    child.on('close', code => resolve({ code: code ?? 1, output: chunks.join('') }))
  })
}

export interface Passthrough {
  cwd: string
  io: Output
  /** Put before each line. */
  prefix: string
}

/**
 * Runs a program with its output passed through, each line prefixed.
 *
 * @param program the executable
 * @param args its arguments
 * @param options where it runs, where its lines go and their prefix
 * @returns its exit code
 */
export function passthrough(program: string, args: string[], options: Passthrough): Promise<number> {
  const { cwd, io, prefix } = options satisfies Passthrough

  return new Promise(resolve => {
    const child = spawn(program, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
    lines(child.stdout, line => io.out(`${prefix}${line}`))
    lines(child.stderr, line => io.err(`${prefix}${line}`))
    child.on('error', error => {
      io.err(`${prefix}${String(error)}`)
      resolve(127)
    })
    child.on('close', code => resolve(code ?? 1))
  })
}
