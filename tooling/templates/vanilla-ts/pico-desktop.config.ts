import { defineConfig } from 'pico-desktop'

// The standard layout needs no more: apps/frontend (Vite), apps/backend (Pico), and the contract
// in packages/contracts/app.cddl (generated into both by `pico-desktop generate`).
export default defineConfig({
  app: { id: '{{appId}}', name: '{{appName}}', version: '0.1.0' },
})
