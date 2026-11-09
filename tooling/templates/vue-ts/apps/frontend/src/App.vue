<template>
  <main>
    <h1>{{appName}}</h1>
    <p>Backend: <span id="state">{{ shownState }}</span> · clock <span id="clock">{{ tick ?? '–' }}</span></p>
    <form id="greet" @submit.prevent="greet(name)">
      <input id="name" v-model="name" autocomplete="off" />
      <button type="submit">Say hello</button>
    </form>
    <p id="message">{{ message }}</p>
  </main>
</template>

<script setup lang="ts">
import type { Transport } from '@pico-desktop/communicator'
import { computed, ref } from 'vue'
import { useBackend } from './backend'

const props = defineProps<{ transport: Transport, mock?: boolean }>()

const { state, message, tick, greet } = useBackend(() => props.transport)
const name = ref('Pico')
const shownState = computed(() => (props.mock ? `${state.value} (mock backend)` : state.value))
</script>
