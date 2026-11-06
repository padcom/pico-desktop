import { defineConfig } from 'vite'

// Relative asset URLs: the host serves the bundle under its trusted origin. The self-test build
// (`--mode self-test`) goes to its own directory so it never ships.
export default defineConfig(({ mode }) => ({
  base: './',
  build: {
    outDir: mode === 'self-test' ? '../dist/self-test' : '../dist/frontend',
    emptyOutDir: true,
  },
}))
