# Linux: what the host needs and how it's verified

Verified on Debian 13 (trixie), Linux 6.12, arm64, X11, CEF 156.0.15 (2026-11-06).

## Building

```sh
pnpm cef:fetch        # downloads and verifies the pinned CEF (toolchain.json) into ~/.cache/pico-desktop/cef
pnpm cef              # configure (preset `cef`), build, build the fixture, ctest (including the self-tests)
```

- CEF's minimal distribution only has `Release/libcef.so`, so the `cef` preset is a Release build.
- CEF's CMake picks `x86_64` on Linux arm64 (it looks for the processor name `arm64`, Linux says
  `aarch64`); the root `CMakeLists.txt` sets `PROJECT_ARCH=arm64` itself.
- CEF compiles its targets with `-fno-exceptions -fno-rtti -fno-threadsafe-statics -Werror`; the
  host's CEF layer is written for that, the CEF-free core (`pd_host_core`) isn't bound by it.
- CEF links with `-rpath,.` (relative to the *working directory*); the host adds `$ORIGIN` so it
  finds `libcef.so` next to itself from anywhere.
- Development builds symlink CEF's binaries and resources next to the executable; packaging (F6)
  copies them.

## Runtime libraries

`libcef.so` needs, besides glibc: `libnss3`, `libnssutil3`, `libsmime3`, `libnspr4`, `libatk-1.0`,
`libatk-bridge-2.0`, `libcups`, `libdrm`, `libxkbcommon`, `libXcomposite`, `libXdamage`,
`libXrandr`, `libXext`, `libXfixes`, `libX11`, `libxcb`, `libgbm`, `libpango-1.0`, `libcairo`,
`libasound`, `libgtk-3`, `libdbus-1`, `libexpat`, `libgio-2.0`, `libudev`. A standard desktop
install has them; the AppImage (F7) has to bundle or require them explicitly.

Rendering without a GPU works through the bundled SwiftShader (`libvk_swiftshader.so`). In a VM
Chromium logs `GetVSyncParametersIfAvailable() failed`; that's harmless.

## The sandbox

The host never sets `--no-sandbox` and never disables it; `CefSettings.no_sandbox = false`.

- It runs on **unprivileged user namespaces** (`kernel.unprivileged_userns_clone = 1`, the Debian
  default). The SUID `chrome-sandbox` helper is shipped but not needed or made setuid.
- `--self-test` proves it from outside: after the page reports, the browser process reads
  `/proc/<pid>/status` of its descendants. Observed:

  | Process | seccomp | own PID namespace |
  |---|---|---|
  | renderer | 2 (filter) | yes |
  | utility (network service) | 2 | yes |
  | zygote (sandboxed) | 0 | yes |
  | gpu-process | 0 | no (normal on Linux) |

  The test fails unless every renderer has seccomp-bpf *and* its own PID namespace.
- **Ubuntu 23.10+ restricts unprivileged user namespaces through AppArmor**
  (`kernel.apparmor_restrict_unprivileged_userns=1`). CI lifts that restriction for its runner
  (`sysctl -w kernel.apparmor_restrict_unprivileged_userns=0`); for users the app needs an
  AppArmor profile that allows `userns` for its executable, or the SUID `chrome-sandbox`.
  Choosing between them is part of the AppImage work (F7).
- Where user namespaces are restricted (some distributions' AppArmor policy, hardened kernels),
  Chromium can't start sandboxed renderers. The host never falls back to `--no-sandbox`; it
  prints a message naming user namespaces when `CefInitialize` fails. **Open (F5/F7):** detect
  the case where initialization succeeds but renderers can't start, and report it the same way;
  the AppImage work tests those configurations explicitly.

## Shutdown

Closing the window closes the browser first (unload handlers run), then the window; the message
loop ends when the last browser is gone. `run-self-test.sh` checks that no host process (renderer,
GPU, utility, zygote) outlives the browser process.

## Not verified yet
- Wayland (only X11 so far); the CI job runs under Xvfb.
- DevTools (`--dev`, F12) and opening external links with a user gesture (`xdg-open`) are
  implemented but not verified yet: a self-test can't press keys or produce a real user gesture.
  (Blocking without a gesture *is* tested.)
