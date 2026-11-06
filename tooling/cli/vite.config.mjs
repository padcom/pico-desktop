import { builtinModules } from 'node:module'
import { defineLibConfig } from '@padcom/vite-config-lib'

import pkg from './package.json'

// A Node program: ES modules only, Node's built-ins and the dependencies stay imports.
export default defineLibConfig(pkg, {
  build: {
    lib: { formats: ['es'] },
    target: 'node26',
    rollupOptions: {
      external: [/^node:/u, ...builtinModules],
    },
  },
})
