// The lint rules of padcom/vue-template (@padcom/eslint-config-vue), for the browser.
import config from '@padcom/eslint-config-vue'

export default [
  { ignores: ['**/dist/**', '**/dist-csp-test/**', '**/generated/**'] },
  ...config['flat/browser'],
]
