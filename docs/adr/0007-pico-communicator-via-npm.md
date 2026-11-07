# ADR-0007: Apps get the Pico Communicator through npm

**Status:** Accepted (F4, 2026-11-07) · **Date:** 2026-11-07

## Context
An app's backend imports the Pico Communicator (`packages/communicator-pico`) as the package
`pico-desktop/communicator`. Pico 0.4's package manager resolves dependencies from workspace members,
git repositories (a whole repository, no subdirectory), or a registry that doesn't exist yet. It has
no local-path dependencies. Vendoring the sources into each app is ruled out
(PROJECT_DESCRIPTION.md §7), and so is a second repository that would have to be kept in sync.

## Decision
**npm delivers the Pico sources.**
- `@pico-desktop/communicator-pico` is an npm package that holds the `.pi` files and their
  `project.json`.
- An app's root `package.json` depends on it, and its Pico workspace (`project.json`) lists
  `node_modules/@pico-desktop/communicator-pico` as a member.
- The backend then depends on `pico-desktop/communicator` by version, like any workspace member.

## Consequences
- One install, `pnpm install`, brings both Communicators at matching versions (docs/versioning.md).
- `project.lock` records the member's resolved (store) path, which differs per machine. The
  template ignores the lock file until Pico can record a path relative to `node_modules`.
- **Revisit when Pico's registry exists (P10.4).** A registry package is the natural replacement,
  and the template would switch to it.
