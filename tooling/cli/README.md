# pico-desktop (the CLI)

```sh
pico-desktop create my-app [--id com.example.my-app] [--framework <checkout>]
cd my-app && pnpm install
pnpm dev        # pico-desktop dev [--trace-ipc]
pnpm test       # pico-desktop test
pnpm generate   # pico-desktop generate
pnpm exec pico-desktop build | package
```

| Command | What it does |
|---|---|
| `create` | scaffolds `tooling/templates/vanilla-ts` and generates its contract. Until the packages are published, it links them from a framework checkout: `--framework`, or by default the checkout the CLI runs from |
| `generate` | turns every contract (CDDL) into the frontend's TypeScript (types, validators, `Services`, `validate`) and the backend's Pico (structs, decoders, encoders). It writes only the files that changed |
| `dev` | starts the app's Vite dev server (HMR), builds the Pico backend and starts the host with `--dev --control=stdin` (see the next section) |
| `test` | regenerates the contracts, then runs the frontend's Vitest and `pico test` on the backend |
| `build` | a portable bundle for this machine: `dist/<name>-<version>-<target>/` with the launcher, `manifest.json`, `sbom.cdx.json`, `app/`, `backend/` and the checked `runtime/` (from `runtime` in the config, `$PICO_DESKTOP_RUNTIME` (a directory, an archive or a URL) or the checkout's staged one) |
| `package` | the bundle as a `.tar.gz` with a `.sha256`; on Linux also an AppDir, and the AppImage when `appimagetool` is available |
| `doctor` | checks Node, the Pico compiler, the host for `dev`, a runtime for `build` and the sandbox's user namespaces, and says what to do about each problem |

What `dev` does after starting:
- **A Pico edit** (a debounced change to `*.pi` or `project.json`) rebuilds the backend into a temporary file. Only a successful build replaces the backend; then the host gets `restart-backend`: the backend receives `closing`, starts again with a new epoch, and the window stays.
- **A failed build** prints the compiler's errors and leaves the running backend alone.
- **A contract edit** regenerates both sides.
- **Ctrl+C** quits the host, which ends the backend within its grace period. The host runs in its own process group, so the shutdown happens in order.

Configuration lives in `pico-desktop.config.ts` at the app's root; Node runs it directly:

```ts
import { defineConfig } from 'pico-desktop'

export default defineConfig({
  app: { id: 'com.example.my-app', name: 'My App', version: '0.1.0' },
  capabilities: ['window'],                         // the host services the backend may use
  window: { title: 'My App', width: 1280, height: 800 }, // default: 1024×768, the page's <title>
  closeGraceMs: 3000,                               // how long the backend has to finish on close
  // the defaults: frontend.root 'apps/frontend', backend.root 'apps/backend', backend.entry 'main.pi',
  // contracts [{ source: 'packages/contracts/app.cddl', typescript: 'apps/frontend/src/generated/app.ts',
  //              pico: 'apps/backend/generated/app.pi' }], host: $PICO_DESKTOP_HOST or the framework's build
})
```

`dev` needs `pico` on the `PATH`. Below 0.4.2 it warns: a pico-lang build with the fixes works. It also needs the host: `host` in the config, `$PICO_DESKTOP_HOST`, or a framework checkout's `build/cef`.
