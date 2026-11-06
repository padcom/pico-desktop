# ADR-0001: A C++ CEF host and a separate Pico process

**Status:** Accepted (design rounds 1–8) · **Date:** 2026-11-06

## Context
Applications are written in two languages: Pico for privileged services and business logic,
TypeScript for the UI. Something has to own windows, Chromium's processes, assets, navigation
and security. Pico has no CEF bindings, and CEF's C++ API, process model and threading rules are
not something application developers should meet.

## Decision
- An internal **C++ host on CEF Views** owns windows, Chromium's browser/renderer/helper processes,
  resource serving, security policy, message routing and backend supervision.
- The application's Pico code is compiled into a **separate OS process**, started and supervised
  by the host and connected to it by native IPC (ADR-0004).
- The host is infrastructure: no application business logic lives in C++. Desktop services that
  need the host (dialogs, clipboard, notifications, window control) are brokered by the host and
  exposed principally as Pico services.

## Consequences
- A Pico crash or hang doesn't take the window down: the host rejects pending calls, restarts the
  backend within a budget and restores subscriptions (PROJECT_DESCRIPTION.md §6).
- Pico needs no FFI to CEF, only process stdio. The capability audit verified exactly that
  (`docs/pico-capabilities.md` §B).
- Every call from the UI to Pico crosses two boundaries (renderer → host → Pico); the latency
  budget is measured in F2 (the audit's stdio round trip is ~55 µs p50).
- `pico-desktop dev` can recompile and restart Pico while the Chromium window stays open.
