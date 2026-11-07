import { defineLibConfig } from '@padcom/vite-config-lib'

import pkg from './package.json' with { type: 'json' }

export default defineLibConfig(pkg)
