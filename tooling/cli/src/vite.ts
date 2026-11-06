// The app's own Vite (resolved from its frontend, so the versions match the app) as a dev server.
import { createRequire } from 'node:module'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'

interface ViteServer {
  listen(): Promise<unknown>
  printUrls(): void
  close(): Promise<void>
  resolvedUrls: { local: string[] } | null
}

interface Vite {
  createServer(options: Record<string, unknown>): Promise<ViteServer>
}

export interface DevServer {
  /** The origin the host loads (`http://localhost:<port>`). */
  url: string
  close(): Promise<void>
}

/**
 * Starts Vite's dev server for the frontend.
 *
 * @param root the frontend's root
 * @returns the server
 */
export async function startDevServer(root: string): Promise<DevServer> {
  const resolved = createRequire(join(root, 'package.json')).resolve('vite')
  const vite = await import(pathToFileURL(resolved).href) as Vite
  const server = await vite.createServer({ root, clearScreen: false, server: { host: 'localhost' } })
  await server.listen()
  server.printUrls()
  const url = server.resolvedUrls?.local[0]?.replace(/\/$/u, '')
  if (!url) {
    await server.close()
    throw new Error('vite started without a local URL')
  }

  return { url, close: () => server.close() }
}
