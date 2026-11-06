# {{appName}}

A [Pico Desktop](https://github.com/padcom/pico-desktop) app: a Vite + TypeScript frontend and a
Pico backend, typed by one contract.

| | |
|---|---|
| `packages/contracts/app.cddl` | the contract: what the two sides say to each other |
| `apps/frontend` | the UI (`src/app.ts` calls the backend; `src/generated/app.ts` is generated) |
| `apps/backend` | the Pico backend (`app.pi` answers; `generated/app.pi` is generated) |

```sh
pnpm install
pnpm dev        # the window, with HMR for the frontend; Pico edits rebuild and restart the backend
pnpm test       # Vitest and pico test
pnpm generate   # after editing the contract (dev does it on save)
```

`pnpm dev` needs `pico` on the PATH and the Pico Desktop host (`PICO_DESKTOP_HOST`, or a
framework checkout's build).
