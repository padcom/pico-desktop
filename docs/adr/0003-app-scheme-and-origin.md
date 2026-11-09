# ADR-0003: The trusted origin is `https://<app-id>.localhost`, served by the host

**Status:** Accepted (F1 experiment, 2026-11-06) · supersedes the working default `app://<app-id>`

## Context
Production apps load their Vite-built assets from the bundle, with no HTTP server. The page needs
a secure origin (for Web APIs and CSP), working ES modules with relative imports, sane CORS, and a
resource handler that can't be tricked into reading outside the bundle. The native bridge must
only be injected into this trusted origin.

Two candidates were implemented in `pico-desktop-host` (`--origin=https|app`) and run through the
same Vite-built self-test fixture (`tests/host-fixture`, `ctest -R host.self-test`), CEF 156,
linux-arm64:

- **`app://<app-id>/`**: a custom scheme registered as standard, secure, CORS- and fetch-enabled,
  served by a CEF scheme handler.
- **`https://<app-id>.localhost/`**: the built-in https scheme, intercepted for that host by the same
  CEF scheme handler.

## Results

Both passed all 24 functional checks: the expected origin, secure context, static and dynamic ES
module imports with `base: './'`, CSS and image assets, `fetch` of bundled files, 404s, four
path-traversal encodings, a CSP header that blocks inline script, the bridge only in the trusted
main frame (not same-origin or `srcdoc` subframes), navigation and popup policy, context cleanup
across navigation, and the renderer sandbox.

The web-platform probes (`pd-info:` lines) differed:

| Feature | `app://` | `https://<id>.localhost` |
|---|---|---|
| localStorage, IndexedDB, `navigator.storage`, clipboard, `crypto` | works | works |
| Cookies (`document.cookie`) | **not stored** (needs `CefCookieManager::SetSupportedSchemes`) | works |
| Cache API | **"Request scheme 'app' is unsupported"** | works |
| Service Workers | **"The URL protocol of the current origin is not supported"** | registers |

Chromium hardcodes the Cache API and Service Workers to http(s); no CEF option fixes them for a
custom scheme.

## Decision
The trusted origin is **`https://<app-id>.localhost`** (the default in the host). It passes
everything without special-casing, and libraries written for the web (offline caches, workers,
auth cookies) work unchanged. `app://<app-id>` stays available behind `--origin=app` and keeps
running in CI, so a regression on either side is visible.

## Why this is safe
- `.localhost` names never resolve off the machine, and the host's scheme handler answers *every*
  request for `<app-id>.localhost`, so nothing for the trusted origin ever reaches the network or
  a local server.
- Other `https` URLs are other origins: top-level navigations to them are blocked or handed to the
  system browser (user gesture only), and the CSP (`connect-src 'self'`, `frame-src 'self'`,
  `script-src 'self'`) keeps the page from fetching or framing them.
- The bridge is injected by origin *and* re-checked in the browser process for every message.
- **WebGL's software fallback depends on this.** On Linux the host passes `--enable-unsafe-swiftshader`
  (page-supplied shaders reach SwiftShader's JIT in the GPU process; docs/graphics.md), which is
  acceptable only because the page is the app's own content under this origin and CSP. If the host
  ever shows content the app does not control (a remote page, a webview of another origin), that
  decision must be made again; `--no-software-webgl` turns the fallback off.

## Consequences
- App IDs must be valid DNS labels (`com.example.app` → `https://com.example.app.localhost/`); the host
  validates them.
- In development the page is the Vite dev server (`http://localhost:<port>`) under the dev policy
  (F4); production never uses a network listener.
