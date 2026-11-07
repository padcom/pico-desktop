# ADR-0008: Native services are host requests, gated by declared capabilities

**Status:** Accepted (F5, 2026-11-07) · **Date:** 2026-11-07

## Context
Apps need some things only the host can do: the window, file dialogs, notifications, quitting. The
architecture puts application logic in Pico (ADR-0001) and forbids exposing unrestricted native
primitives to JavaScript (docs/architecture.md, NativeServices).

## Decision
- **The backend calls them as ordinary requests named `host.…`.** The router recognises the prefix
  on requests *from the backend* and gives them to the host instead of a page. Pages can't reach
  them. No protocol change was needed.
- **They're described by a contract** (`packages/communicator-pico/contracts/host.cddl`), with a
  typed wrapper, `Host`, generated from it.
- **Each group needs a capability the app declares** in `pico-desktop.config.ts`: `window`,
  `dialogs` or `notifications`. The host gets them as `--capabilities`. Anything else fails with
  `NOT_ALLOWED`, and unknown names with `UNKNOWN_METHOD`. `host.app.*` needs none.
- **An answer goes only to the backend process that asked.** A dialog that closes after a restart
  answers no one.

## Consequences
- The permission model is visible in one line of the app's config, and the SBOM and manifest
  record it.
- New services need a contract entry, a table entry in the host (`services.cpp`), and an
  implementation per platform.
- The clipboard isn't there: CEF has no API for it.

## Addendum (2026-11-07): the page's permission prompts

Chromium asks for permission before some web APIs: reading the clipboard, the camera, location,
notifications. A desktop app has no prompt UI, and CEF (Alloy) leaves an unanswered prompt pending
forever, so `navigator.clipboard.readText()` never settled. The host now answers every prompt:
- it grants only reading the clipboard, only to the app's own origin, and only when the app
  declares a fourth capability, `clipboard`;
- it denies everything else and logs it.

Writing the clipboard needs no permission. Native features beyond these stay host services that
the backend calls.

## Addendum (2026-11-07): the backend's clipboard

`host.clipboard.writeText` and `host.clipboard.readText`, with the page's rule: writing needs no
capability, reading needs `clipboard`. CEF has no clipboard API, so on Linux the host is an X11
client of its own (`src/cef/clipboard.cpp`): XCB, on a connection and thread of its own, so it
shares nothing with Chromium's X11 code (Xlib's process-wide error handler is why it isn't
Xlib). It owns the CLIPBOARD selection while it holds text and answers other programs' requests
(UTF8_STRING, TEXT, STRING as Latin-1, TARGETS, TIMESTAMP); reading converts the selection,
chunked (INCR) transfers included. Text is limited to 1 MiB each way. Under Wayland it's XWayland,
which keeps both clipboards in sync; without any X server the service is `UNAVAILABLE`.

X11 text lives as long as its owner: when the app quits, the host hands its text to the
clipboard manager (ICCCM `SAVE_TARGETS`) if the desktop runs one; otherwise it goes with the app.
