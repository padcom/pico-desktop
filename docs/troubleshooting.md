# Troubleshooting

Start with `pnpm exec pico-desktop doctor` in your app. It checks what development and builds need, and prints the fix for each problem.

| What you see | Why | What to do |
|---|---|---|
| `Chromium's sandbox can't start here: this system doesn't let unprivileged processes create user namespaces…` | The renderer sandbox needs user namespaces (or a setuid-root helper), and the host never runs without it | Ubuntu 23.10+: `sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0`, or an AppArmor profile granting `userns`. Debian and others: `kernel.unprivileged_userns_clone=1` and a nonzero `user.max_user_namespaces`. Or make `runtime/chrome-sandbox` root-owned with mode 4755. See [platform-linux.md](platform-linux.md) |
| `pico-desktop-host: <app> is already running; its window was brought to the front` | Single instance: one process per app profile | Expected. Use `--multi-instance` for separate instances, each with its own temporary profile |
| `dev │ no pico on the PATH` | `pnpm dev` and `pnpm test` build the backend with `pico` | Install Pico ≥ 0.4.2, or put a pico-lang build first on the `PATH` ([toolchain.md](toolchain.md)) |
| `dev │ pico 0.4.1 is older than 0.4.2…` | Released 0.4.1 lacks fixes the framework needs (pico-lang #82: stdout to a pipe) | Use a newer build. The warning is harmless with a pico-lang build that has them |
| `dev │ no pico-desktop-host` | `dev` runs the host from `host` in the config, `$PICO_DESKTOP_HOST` or a framework checkout's `build/cef` | Set one of them, or run `pnpm cef` in the checkout |
| `pico-desktop build: no Pico Desktop runtime for linux-arm64` | `build` bundles a staged runtime | Run `pnpm cef && pnpm runtime:stage` in a framework checkout, or point `runtime` / `$PICO_DESKTOP_RUNTIME` at a runtime directory, archive or URL |
| `the runtime in … was changed or is incomplete: <file> doesn't match runtime.json` / `…has a file runtime.json doesn't list…` | The runtime's files don't match its manifest | Use an intact runtime: restage it, or download it again (delete it from `~/.cache/pico-desktop/runtimes`) |
| `… doesn't match its SHA-256 (….sha256): refusing it` | A downloaded or copied runtime archive is corrupt or was altered | Download it again |
| `cannot import pico-desktop/communicator` (E0341) in a backend | A workspace import from a relative file path (pico-lang #93, fixed by PR #94) | Build with an absolute path (the CLI does), or with a pico that has the fix |
| `field defaults isn't implemented yet` / `missing field` in generated Pico | An older generator wrote optional fields as `T?` | Regenerate (`pnpm generate`): optional fields are `name?: T` now |
| Requests fail with `DISCONNECTED` | The backend isn't ready, or it stopped while they were pending | Watch the host's log (`pd-host: …`). After a crash it restarts by itself; after 5 restarts in a row it stops (`failed`): fix the crash, then save (`dev` restarts it) |
| Requests fail with `BUSY` | The backend is behind: 1024 messages or 16 MiB wait to be written to it | Slow down, or make the handler faster. A hung backend is restarted after 15 s |
| `NOT_ALLOWED` from a host service | The app didn't declare that capability | Add it to `capabilities` in `pico-desktop.config.ts` |
| `UNAVAILABLE` from `host.notification.show` | There's no `notify-send` (libnotify) | Install `libnotify-bin`, or treat notifications as optional |
| Many `dbus` errors in the log | No session bus in the environment (CI, `env -i`) | Harmless; a desktop session has one |
| `GetVSyncParametersIfAvailable() failed` | Chromium GPU noise on some X11 setups | Harmless; `pnpm dev` hides it |
