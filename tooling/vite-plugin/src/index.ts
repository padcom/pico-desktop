import type { Plugin } from 'vite'

/**
 * The Vite plugin for Pico Desktop frontends: dev-server integration with the native host and the
 * production asset layout (F4/F6). For now it only registers itself.
 *
 * @returns the plugin
 */
export function picoDesktop(): Plugin {
  return {
    name: 'pico-desktop',
  }
}
