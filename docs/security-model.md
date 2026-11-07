# Security model

There are three parties:
- **The page** runs untrusted-by-default web code in Chromium's sandboxed renderer.
- **The host** (`pico-desktop-host`) is trusted. It owns the window, the policy and the routing.
- **The backend** is the app's own compiled Pico process. It's as trusted as the app.

The host never runs the page unsandboxed, never exposes general native primitives to it, and never lets one page or backend run speak for another.

## The page

| Rule | How |
|---|---|
| Chromium's sandbox, always | `no_sandbox = false`; there's no option to disable it. Renderers run under seccomp-bpf in their own PID namespace, and the self-test checks that from `/proc`. Without user namespaces (and no setuid helper) the host refuses to start and says why ([platform-linux.md](platform-linux.md)) |
| One trusted origin | `https://<app-id>.localhost` (ADR-0003), served from the bundle with path confinement (no traversal, no symlinks out) and a CSP (`script-src 'self'`). In development, also the local Vite origin |
| The bridge only where it's trusted | `window.picoDesktop` exists only in the trusted origin's main frame: never in subframes or other origins. The browser process checks the sender again, since the renderer is the less trusted side |
| Navigation | Leaving the origin is blocked. Web links open in the system browser, and only on a user gesture. `file:` and other schemes never load. Popups never open in the app |
| No remote debugging in production | Remote-debugging switches are refused without `--dev`. DevTools (F12) exists only with `--dev` |
| Bounded messages | 1 byte to 4 MiB per message, checked in the renderer and again in the browser process. A message that breaks the CBOR profile or the envelope rules is dropped. A page can't address another window (`w`) |
| Its own session | Each page context is one session. Request IDs are renumbered per session, so answers never cross pages. A page that goes away has its calls cancelled and its subscriptions closed |

## The backend

| Rule | How |
|---|---|
| Only registered handlers | A name nobody registered is `UNKNOWN_METHOD`, on both sides |
| Typed payloads | The generated decoders refuse what the contract doesn't allow (`INVALID_PAYLOAD`) before a handler runs. The frontend's generated validators do the same for answers |
| Native services by capability | The backend reaches the host's services (window, dialogs, notifications) only for capabilities the app declares. Anything else is `NOT_ALLOWED`, and unknown services `UNKNOWN_METHOD`. Pages can't call them at all |
| Page permission prompts | Chromium's prompts (camera, microphone, location, notifications, clipboard read, …) have no UI in a desktop app, and unanswered they leave the page's promise pending forever. The host answers every one: it grants only reading the clipboard, only to the app's own origin, and only with the `clipboard` capability. Everything else is denied and logged (`pd-host: denied a permission request …`) | `HostClient::OnShowPermissionPrompt`, `permission_granted` (unit-tested), checked in a real window |
| Bounded queue | While 1024 messages or 16 MiB wait to be written to the backend, pages' new requests get `BUSY` |
| Supervised | A crash or a hang (no `pong` for 15 s) is restarted with backoff, at most 5 times in a row. After that the page is told `failed`. Calls pending across a restart fail with `DISCONNECTED` and are never replayed |
| Closed in order | On close the backend gets `closing` and 3 s to finish, then it's killed. Nothing outlives the host |

## The bundle

- **The runtime is checked file by file.** `build` uses a runtime only if every file matches its `runtime.json` (SHA-256), it has no unlisted files, and it's for the right target and protocol. A downloaded runtime must match its `.sha256`.
- **`package` writes a `.sha256` next to the archive.** The bundle's `manifest.json` lists every app and backend file's hash, and `sbom.cdx.json` lists what's inside.
- **Nothing is signed yet.** macOS and Windows signing and notarization are deferred (F7), so on those systems expect first-launch warnings. See [First launch of unsigned builds](#first-launch-of-unsigned-builds).

## Not covered (yet)

- **The page's own JavaScript** is the app's responsibility, as on the web: the CSP and the origin limit it.
- **A backend is as powerful as its user.** Pico code can use the file system and the network directly; capabilities limit only what it asks *the host* for.
- **Not built yet:** clipboard access, and a per-capability consent UI.

## First launch of unsigned builds

Until signing and notarization are in place (F7), the operating systems treat Pico Desktop apps as
coming from an unidentified developer. What users see, and what to tell them:

| System | What happens | What the user does |
|---|---|---|
| macOS | Gatekeeper refuses to open the app downloaded from the internet ("cannot be opened because Apple cannot check it for malicious software") | Control-click the app, choose **Open**, then **Open** again, once. Or remove the quarantine attribute: `xattr -dr com.apple.quarantine <app>` |
| Windows | SmartScreen shows "Windows protected your PC" | **More info**, then **Run anyway**, once |
| Linux (AppImage) | Nothing is checked, but the file isn't executable after downloading | `chmod +x <app>.AppImage`. It also needs FUSE 2; without it, `--appimage-extract` unpacks it |
| Linux (bundle) | Nothing is checked | Run `<bundle>/<name>` |

Publish the SHA-256 of every download (`pico-desktop package` writes `.sha256` files), so users can
check what they got before overriding the warning. Signing replaces these steps; it doesn't change
the sandbox, origin or capability rules above.

