import { resolve } from 'node:path'
import { defineConfig } from 'vite'

// Relative asset URLs (`base: './'`) on purpose: the host must serve them from the bundle.
export default defineConfig({
  base: './',
  build: {
    rollupOptions: {
      input: {
        index: resolve(import.meta.dirname, 'index.html'),
        second: resolve(import.meta.dirname, 'second.html'),
      },
    },
  },
})
