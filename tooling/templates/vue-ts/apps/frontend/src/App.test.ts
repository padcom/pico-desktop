import { flushPromises, mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import App from './App.vue'
import { mockBackend } from './mock-backend'

const sleep = (ms: number) => new Promise(resolve => {
  setTimeout(resolve, ms)
})

describe('the app', () => {
  it('connects to the backend', async () => {
    const wrapper = mount(App, { props: { transport: mockBackend() } })
    await sleep(10)
    await flushPromises()
    expect(wrapper.get('#state').text()).toBe('ready')
    wrapper.unmount()
  })

  it('greets through the backend', async () => {
    const wrapper = mount(App, { props: { transport: mockBackend(), mock: true } })
    await wrapper.get('#name').setValue('Vue')
    await wrapper.get('form').trigger('submit')
    await sleep(10)
    await flushPromises()
    expect(wrapper.get('#message').text()).toBe('Hello, Vue! (from the mock backend)')
    expect(wrapper.get('#state').text()).toContain('(mock backend)')
    wrapper.unmount()
  })
})
