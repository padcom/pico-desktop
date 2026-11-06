import config from '@padcom/eslint-config-typescript'
import globals from 'globals'

/** @type {import('eslint').Linter.Config[]} */
export default [
  {
    ignores: [
      '**/dist/**',
      '**/coverage/**',
      'build/**',
      'probes/bin/**',
      // Pico sources (`.pi`) look like TypeScript but aren't; `pico lint` checks them.
      '**/*.pi',
    ],
  },
  ...config['flat/node'],
  {
    // Runs in the CEF renderer (and in a plain browser with a mock transport).
    files: ['packages/communicator-ts/**', 'tests/host-fixture/src/**', 'tests/host-fixture/public/**'],
    languageOptions: {
      globals: { ...globals.browser },
    },
  },
  {
    rules: {
      'jsdoc/require-jsdoc': 'off',
    },
  },
]
