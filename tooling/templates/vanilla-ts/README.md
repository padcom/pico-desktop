# {{appName}}

A [Pico Desktop](https://github.com/padcom/pico-desktop) app: a Vite + TypeScript frontend and a
Pico backend, typed by one contract.

| | |
|---|---|
| `packages/contracts/app.cddl` | the contract: what the two sides say to each other |
| `apps/frontend` | the UI (`src/app.ts` calls the backend; `src/generated/app.ts` is generated; `src/mock-backend.ts` stands in for it in a plain browser and in Vitest) |
| `apps/backend` | the Pico backend (`app.pi` answers; `generated/app.pi` is generated) |

```sh
pnpm install
pnpm dev        # the window, with HMR for the frontend; Pico edits rebuild and restart the backend
pnpm ui         # just the frontend, in any browser, against a TypeScript mock backend
pnpm test       # Vitest and pico test
pnpm e2e        # end to end: the real window and backend, driven by apps/frontend/e2e/*.e2e.ts
pnpm generate   # after editing the contract (dev does it on save)
```

`pnpm dev` needs `pico` on the PATH and the Pico Desktop host (`PICO_DESKTOP_HOST`, or a
framework checkout's build).

In VS Code, the `dev` and `test` tasks run the same commands (Terminal → Run Task).
`pnpm dev --trace-ipc` logs every message between the window and the backend (envelopes only,
never payloads), and F12 opens DevTools.

To debug the backend in VS Code, start **Debug the backend** (F5), then run `pnpm dev --debug`.
The debugger attaches to the backend when it starts, and breakpoints in `.pi` files work. With
`--debug` the host stops pinging the backend, so a paused backend isn't taken for a hung one
and killed. Every rebuild starts a new backend process: to follow it, start **Debug the
backend** again, then save.
