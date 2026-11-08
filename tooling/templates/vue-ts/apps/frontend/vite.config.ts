import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vitest/config'

// The production CSP is `script-src 'self'; style-src 'self'`, and this build satisfies it:
// - the runtime-only Vue build (no template compiler, so no `new Function`/eval) with the SFC
//   templates compiled at build time by @vitejs/plugin-vue;
// - the CSS extracted to a file and linked from index.html (no <style> injected at run time);
// - the Vue feature flags set here, so the bundler build doesn't warn about them.
// `pnpm dev` loads Vite's own origin, which has no host CSP, so Vite's injected styles and HMR
// work there; the production policy isn't weakened for it (see docs/tutorial.md).
export default defineConfig({
  // Relative asset URLs: the host serves the built frontend from its own origin.
  base: './',
  plugins: [vue()],
  define: {
    __VUE_OPTIONS_API__: 'false',
    __VUE_PROD_DEVTOOLS__: 'false',
    __VUE_PROD_HYDRATION_MISMATCH_DETAILS__: 'false',
  },
  build: {
    cssCodeSplit: false,
    // Scripts only from files, never inlined as data: URLs.
    assetsInlineLimit: 0,
  },
  test: {
    environment: 'jsdom',
    include: ['src/**/*.test.ts'],
  },
})
