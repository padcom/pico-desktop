# ADR-0003: A registered secure custom scheme for bundled assets

**Status:** Proposed; decided by the F1 experiment · **Date:** 2026-11-06

## Context
Production apps load their Vite-built assets from the bundle, with no HTTP server. The page needs
a secure origin (for Web APIs and CSP), working ES modules with relative imports, sane CORS, and a
resource handler that can't be tricked into reading outside the bundle. The native bridge must
only be injected into this trusted origin.

## Decision (working default)
Register a **custom standard + secure scheme** (`app://<app-id>/…`) served by CEF resource
handlers from the bundle's asset directory.

The alternative is an **HTTPS-like internal origin** (for example `https://<app-id>.localhost/`) intercepted by CEF
handlers. F1 tests both against: ES module loading and relative imports, `fetch` of bundled
files, CSP, CORS behaviour, Service Workers/storage partitioning, DevTools source maps, and path
traversal attempts. The one that passes with less special-casing wins; this ADR is updated with
the results.

## Consequences
- The bridge's origin allowlist is a single trusted origin per app (plus the Vite dev-server
  origin under the development policy only).
- External URLs never load in the app window: they open in the system browser.
