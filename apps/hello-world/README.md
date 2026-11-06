# hello-world

The F3 vertical slice: a Vite + TypeScript frontend and a compiled Pico backend. They talk typed CBOR through the CEF host, with no MAVLink involved.

| | |
|---|---|
| `contracts/hello.cddl` | the contract (ADR-0005), the single source of truth |
| `frontend/src/generated/hello.ts` | generated types, validators, `Services` and the Communicator's `validate` hook |
| `backend/generated/hello.pi` | generated Pico structs, decoders and encoders |
| `frontend/src/app.ts` | the app's logic, without the DOM, unit-tested against a scripted backend (`app.test.ts`) |
| `frontend/src/view.ts` | the page (it asks in an in-page panel, never a native dialog) |
| `frontend/src/self-test.ts` | the end-to-end checks, compiled only into `--mode self-test` builds |
| `backend/app.pi` | the handlers (`greeting.hello`, `greeting.ask`) and the `clock.ticks` publisher, tested over a memory link in `app.test.pi` |
| `backend/main.pi` | the process: the Communicator on stdin/stdout |

## What it shows

- **A button → typed RPC → a Pico handler → the response → the UI.** `greeting.hello` returns `{message, count}`.
- **A call from Pico to a TypeScript handler.** `greeting.ask` calls `ui.confirm` in the window that asked, before it answers.
- **A one-way event.** After each greeting the backend emits `greeting.greeted`, and the count on the page comes from it.
- **A managed subscription.** `clock.ticks` ticks every `intervalMs`. After a backend restart it re-subscribes by itself, with nothing replayed.
- **Typed errors.**
  - `EMPTY_NAME` comes from the handler.
  - `INVALID_PAYLOAD` comes from the generated Pico decoder, and from the TS validators for answers that break the contract.
  - `UNKNOWN_METHOD` is returned for names nobody registered.

## Run it

Run these from this directory. They need the CEF host built (`pnpm cef` at the root) and pico ≥ 0.4.2 on the `PATH`; until 0.4.2 is released, use a pico-lang build.

```sh
pnpm generate     # after changing contracts/hello.cddl
pnpm start        # build the frontend and the backend, open the window
pnpm self-test    # the end-to-end test in a real window (also a ctest: hello-world.self-test)
pnpm test         # frontend unit tests (Vitest)
pnpm backend:test # backend tests and lint (pico)
```

The self-test runs the real host, the real backend and the real page. It checks:
- every item listed under "What it shows";
- a backend crash: the host's self-test-only `host.killBackend` kills the backend. The pending call then fails with `DISCONNECTED` and the window stays. The host restarts the backend (epoch 2), the subscription resumes from a fresh publisher, and the new backend's counter starts again at 1;
- the renderer sandbox;
- that the backend exits by itself when the window closes, and that no process outlives the host (`scripts/self-test.sh`).

The backend scripts pass absolute paths to `pico build` and `pico test`. A bare file name inside a workspace member misses the workspace (pico-lang #93).
