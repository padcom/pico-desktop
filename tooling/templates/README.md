# templates

The `pico-desktop create` templates.

**`vanilla-ts`:** a Vite + TypeScript frontend, a Pico backend and a CDDL contract (`packages/contracts/app.cddl`) that is generated into both sides.
- **Placeholders.** `{{name}}`, `{{appId}}` and `{{appName}}` are filled in at create time. So are `{{dep:…}}` (for now, `link:` paths into a framework checkout) and `{{version:…}}` (the framework's own tool versions).
- **`_gitignore`** becomes `.gitignore`.
- **The Pico communicator comes through npm:**
  - the root `package.json` depends on `@pico-desktop/communicator-pico`, which ships the `.pi` sources;
  - the Pico workspace (`project.json`) lists `node_modules/@pico-desktop/communicator-pico` as a member;
  - the backend then imports `pico-desktop/communicator`, with no vendored sources and the same version as the TypeScript packages.

The framework's ESLint skips this directory. `tooling/cli/tests/dev-loop.sh` (ctest `cli.dev-loop`) checks the template instead: it creates an app, installs it, runs its tests and its typecheck, and runs `pnpm dev`.
