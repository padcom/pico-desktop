#!/usr/bin/env node
import { main } from '../dist/index.js'

process.exitCode = main(process.argv.slice(2), {
  out: text => process.stdout.write(`${text}\n`),
  err: text => process.stderr.write(`${text}\n`),
})
