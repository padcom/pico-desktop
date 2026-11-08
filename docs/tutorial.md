# Tutorial: your first Pico Desktop app

You'll scaffold an app, run it with live reload, add a method to its contract, implement it in Pico, call it from TypeScript, then build a bundle you can hand to someone else. Everything here runs on Linux arm64 today; other targets come later ([ADR-0006](adr/0006-six-target-artifacts.md)).

**You need:**
- Node.js and pnpm (versions in [`toolchain.json`](../toolchain.json));
- `pico` ≥ 0.4.3 on your `PATH` (a pico-lang release; see [toolchain.md](toolchain.md));
- the Pico Desktop host. Until the packages are published, use a checkout of this repository after `pnpm install && pnpm build && pnpm cef`.

## 1. Create

```sh
node <checkout>/tooling/cli/bin/pico-desktop.mjs create my-app --id com.example.my-app
cd my-app
pnpm install
```

| | |
|---|---|
| `packages/contracts/app.cddl` | the contract: every method, event and subscription, with its types |
| `apps/frontend/` | Vite + TypeScript. `src/app.ts` is the logic, `src/main.ts` the page, `src/mock-backend.ts` stands in for the backend in a browser and in tests |
| `apps/backend/` | Pico. `app.pi` has the handlers, `main.pi` the process |
| `pico-desktop.config.ts` | the app's ID, name, version and capabilities |

`pico-desktop.config.ts` takes:

| Option | What it sets | Default |
|---|---|---|
| `app` | `id` (reverse-DNS), `name`, `version` | name = id, `0.0.0` |
| `capabilities` | the host services the backend may call: `window`, `dialogs`, `notifications`; and `clipboard`, which lets the page (`navigator.clipboard.readText`) and the backend (`host.readClipboard()`) read the clipboard; writing needs nothing | none |
| `window` | `title` (fixed), `width`, `height` | the page's `<title>`, 1024×768 |
| `closeGraceMs` | how long the backend has to exit when the app closes | 3000 |
| `liveness` | `{ intervalMs, timeoutMs }`: how often the host pings the backend, and after how long without an answer it counts as hung and is restarted; `false` turns pings off | 5000/15000 |
| `icon` | the AppImage's icon (.png or .svg) | a plain one |
| `frontend`, `backend` | `root` (and the backend's `entry`) | `apps/frontend`, `apps/backend` with `main.pi` |
| `host`, `runtime` | where the host and the runtime to bundle are | `$PICO_DESKTOP_HOST` / `$PICO_DESKTOP_RUNTIME`, then the framework's |

## 2. Run it

```sh
pnpm dev
```

A window opens:
- **Frontend edits** are hot-reloaded (CSS) or reload the page.
- **Backend edits** rebuild the Pico backend, and the host restarts it in the same window.
- **A syntax error** prints the compiler's message and keeps the old backend running until you fix it.

`pnpm dev --trace-ipc` logs every message's envelope (never its payload), and F12 opens DevTools. To debug the backend, start VS Code's **Debug the backend** first (the template's `.vscode/launch.json`), then run `pnpm dev --debug`. The debugger attaches when the backend starts, and `--debug` turns off the host's liveness pings, so a backend paused at a breakpoint isn't killed as hung. `pnpm ui` runs the frontend alone in any browser, against the mock backend.

### Using Vue

`create my-app --template vue-ts` scaffolds the same app with a Vue 3 + TypeScript frontend: `App.vue` and a `useBackend()` composable (`src/backend.ts`) over the generated contract and the Communicator. Tests are Vitest with `@vue/test-utils`, `pnpm typecheck` runs `vue-tsc` over the templates, and `pnpm e2e` also checks that the page logged no CSP violation. The rest of this tutorial applies unchanged, except that the Vue template has no `.vscode` folder yet (no **Debug the backend** configuration). It also turns the Vue Options API off (`__VUE_OPTIONS_API__` is `false` in dev, tests and production), so write components with `<script setup>`.

The production page runs under `script-src 'self'; style-src 'self'`, so the template builds for it:
- the **runtime-only** Vue build, with the SFC templates compiled at build time by `@vitejs/plugin-vue`: no `eval` or `new Function`;
- CSS **extracted** to a file and linked from `index.html`; use `<style scoped>` freely, but never write `style="…"` attributes or `setAttribute('style', …)` (bind a class, or use the `style` property's CSSOM, which the policy allows);
- no inline `<script>` or `<style>`, and the Vue feature flags set in `vite.config.ts`.

`src/build.test.ts` builds the frontend and fails if any of that regresses.

`pnpm dev` is different on purpose: the window loads Vite's own origin, which the host sends no CSP for, so Vite's injected `<style>` tags and HMR (an inline client script and a WebSocket) work as usual. The production policy isn't loosened for it; it applies to the built app (`pnpm build`, `pnpm e2e`).

## 3. Add a method

Add a method to the contract:

```cddl
services = {
  "greeting.hello": ["method", hello-request, greeting],
  "clock.ticks": ["subscription", clock-params, tick],
  "math.add": ["method", { a: int, b: int }, { sum: int }],
}
```

`pnpm dev` regenerates both sides as you save: `apps/frontend/src/generated/app.ts` (types, validators, the typed `Services`) and `apps/backend/generated/app.pi` (structs, decoders, encoders). Inline maps get named types, here `MathAddRequest` and `MathAddResponse`.

Implement it in `apps/backend/app.pi`:

```ts
async function add(payload: Cbor?, _ctx: RequestContext): Promise<Cbor?> throws Error {
  const request = decodeMathAddRequest(payload ?? Cbor.Null, "$")
  encodeMathAddResponse(MathAddResponse { sum: request.a + request.b })
}
```

Register it in `createBackend`: `c.handle("math.add", add)`. Pico has no async closures yet, so handlers are named async functions. A payload that doesn't fit the contract never reaches your code: the generated decoder refuses it with `INVALID_PAYLOAD`.

Call it from `apps/frontend/src/app.ts`. The name and both payloads are type-checked against the contract:

```ts
const { sum } = await this.communicator.request('math.add', { a: 2, b: 3 })
```

Save, and the backend restarts in the same window.

## 4. Use the host's services from Pico

The backend can ask the host for native things through `Host`, from `pico-desktop/communicator`:

```ts
const host = new Host(c)
await host.setTitle("My App: 3 files")
const paths = await host.openFile(OpenFile { title: "Open", multiple: true })
```

Each group needs a capability declared in `pico-desktop.config.ts` (`capabilities: ['window', 'dialogs', 'notifications', 'clipboard']`); anything else fails with `NOT_ALLOWED`. The clipboard: `await host.writeClipboard("text")` needs no capability, `await host.readClipboard()` (`null` when there's no text) needs `clipboard`. The full list is in [`contracts/host.cddl`](../packages/communicator-pico/contracts/host.cddl).

## 5. Test

```sh
pnpm test
```

This runs the frontend's Vitest tests (against the mock backend) and the backend's `pico test` (over an in-memory link).

End to end, in the real window with the real backend:

```sh
pnpm e2e
```

`pnpm e2e` (`pico-desktop test --e2e`) builds the frontend and the backend, then runs `apps/frontend/e2e/*.e2e.ts` in Vitest under Node, one file at a time. In a test, `launchApp()` (from `pico-desktop`) starts the app, and the page is driven over the DevTools protocol:

```ts
import { launchApp } from 'pico-desktop'

const app = await launchApp()
await app.fill('#name', 'end to end')
await app.click('button[type=submit]')
expect(await app.waitForText('#message', 'Hello')).toBe('Hello, end to end!')
expect(await app.close()).toBe(0)  // closed like its window; 0 when the backend exits in time
```

It has `evaluate(js)`, `text`, `click` (a real mouse click, so a user gesture), `fill`, `press(key)` (through the host's key handling first, as the keyboard's are), `waitFor(js)`, `waitForText` and `devtools(method, params)` for any other DevTools protocol command; `log` has what the host printed (the page's console as `console: …`). The window opens in development mode (remote debugging is never on in production) with its own profile, under the app ID plus `.e2e`. It needs a display; in CI, run it under `xvfb-run`.

## 6. Build and package

```sh
pnpm exec pico-desktop build     # dist/my-app-0.1.0-linux-arm64/
pnpm exec pico-desktop package   # dist/my-app-0.1.0-linux-arm64.tar.gz (+ .sha256)
```

The bundle runs on a machine with nothing installed beyond the system libraries Chromium needs: no Node, pnpm, Pico or CMake. Its contents:

| | |
|---|---|
| `my-app` | the launcher |
| `app/` | the built frontend |
| `backend/` | the release build of the Pico backend |
| `runtime/` | the host and CEF, with their licenses |
| `manifest.json` | what's in it, with SHA-256s |
| `sbom.cdx.json` | a CycloneDX SBOM |

`build` refuses a runtime whose files don't match its manifest. Unpack the archive anywhere and run `./my-app`.

`package` also writes the AppImage's directory, `dist/my-app-0.1.0-linux-arm64.AppDir`. Its `AppRun` runs the app the same way. With [`appimagetool`](https://github.com/AppImage/appimagetool) on the `PATH` (or `PICO_DESKTOP_APPIMAGETOOL` naming it), `package` squashes the AppDir into `my-app-0.1.0-aarch64.AppImage` with a `.sha256`. The CLI never downloads it. Set `icon` in the config to a .png or .svg; otherwise you get a plain one. That last step hasn't been run here yet (PLAN.md, F7).
