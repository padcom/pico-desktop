# Graphics: WebGL and canvas in the host

An app that draws a map (WebGL) or instruments (canvas 2D) needs to know what the host gives it, with a
GPU and without one. The host's self-test answers that, in every CI run.

## What the self-test checks

`pico-desktop-host --self-test` loads the fixture page (`tests/host-fixture`), which in
`src/graphics.ts`:

1. **creates a WebGL context** (WebGL 2, else WebGL 1) and reports the renderer;
2. **animates a WebGL canvas** (20 000 points moved every frame, a map-like load) for one second and
   counts the animation frames;
3. **animates a 2D canvas** (300 circles moved every frame, an instrument-like load) the same way.

The output has one `pd-info:` line per fact, and `run-self-test.sh` (ctest `host.self-test.app` and
`host.self-test.https`) prints a summary line:

```
console: pd-info: webgl.api=webgl2
console: pd-info: webgl.renderer=<the unmasked renderer string: the GPU, or SwiftShader / llvmpipe>
console: pd-info: webgl.vendor=...
console: pd-info: webgl.version=...
console: pd-info: webgl.software=true|false     (the renderer's name says it runs on the CPU)
console: pd-info: webgl.maxTextureSize=...
console: pd-info: webgl.fps=59.9 (61 frames, the slowest 19 ms)
console: pd-info: canvas2d.fps=60.0 (61 frames, the slowest 18 ms)
graphics: webgl2, renderer '...', software rendering: false, WebGL 59.9 (...), 2D canvas 60.0 (...)
```

`pnpm cef` runs it (`pnpm cef:test`, ctest), so the renderer it used is in its output.

### What fails and what is only reported

| | Linux (x64, arm64) | macOS, Windows |
|---|---|---|
| no WebGL context at all | **fails** `WebGL context is created` | reported (`webgl.api=none`) |
| fewer than 10 animation frames per second | **fails** `... gets frames` | reported |
| the renderer, whether it is software, the frame rate | reported | reported |

The frame rate is **not asserted** beyond "frames arrive": it depends on the machine, and CI has no
GPU. 10 frames per second only catches a compositor that isn't delivering frames at all. To judge 60 fps,
read the `fps` lines of a run on the hardware you care about.

## What an app can expect

- **With a GPU:** hardware WebGL and GPU compositing, as in Chrome. `webgl.software` is `false`.
- **Without one** (a VM, a container, CI under Xvfb): canvas 2D is software-rendered (60 fps for the
  load above in CI), and WebGL runs on the CPU through SwiftShader, which the host allows on Linux
  with `--enable-unsafe-swiftshader`; without that flag there is no WebGL (see "Results"). The
  self-test says which renderer drew (`webgl.renderer`, `webgl.software=true`) and how many frames
  per second it managed for the two loads above.
- **An app must not assume 60 fps.** Check `renderer` and the measured frame rate at start (the same
  `WEBGL_debug_renderer_info` call), and have a cheaper mode: fewer points, no antialiasing,
  or a 2D canvas.
- Context creation can fail (no GPU and no software fallback, a blocklisted driver): the app needs a
  message and a 2D fallback, never a blank window.
- The page runs under the production CSP (`script-src 'self'`): shaders are strings given to
  `gl.shaderSource`, which the policy doesn't cover, so WebGL needs no relaxation.

## What was measured, and where

Honest account of the state when this was written:

- **Not measured on the author's machine.** The development machine (linux-arm64, no GPU, no CEF host
  build) cannot run `pico-desktop-host`; the code was typechecked, linted and the fixture built there,
  and nothing more. **Every number in this document is from a CI run on a hosted runner**, never from
  a machine with a real GPU.
- **Where it runs:** CI, in the jobs "CEF host, linux-x64" and "CEF host, linux-arm64" (a native runner
  each, under Xvfb, so software rendering), in the ctest `host.self-test.app` and `host.self-test.https`
  (once per origin, so twice per job). It also runs in the macOS and Windows jobs, where it only reports.
- **The CI results** (renderer, `software`, frames per second) are in "Results" below.
- **Real GPUs are not covered by CI.** On a developer machine with one, run `pnpm cef` and read the
  `graphics:` line.

## Results

**First CI run (run 42543593827 of the pull request that added the self-test), before the host did
anything about it: linux-x64 and linux-arm64, Xvfb, no GPU.**

| | linux-x64 | linux-arm64 |
|---|---|---|
| WebGL context (WebGL 2 or 1) | **none could be created** (`webgl.api=none`) | **none could be created** |
| 2D canvas, 300 circles | 60.0 fps (61 frames, the slowest 17 ms) | 60.0 fps (62 frames, the slowest 17 ms) |

So with no GPU the host as it was gave a page software-rendered canvas 2D, at a full 60 fps for that
load, and **no WebGL at all**: Chromium does not fall back to its software renderer for WebGL on its
own any more. The map could not have worked in CI (or in a VM or container) like that.

The host now passes `--enable-unsafe-swiftshader` on Linux (`HostApp::OnBeforeCommandLineProcessing`),
which allows that fallback. What the name means: SwiftShader was not hardened against shaders from an
untrusted page, because it compiles them with a JIT (Subzero on x64, LLVM on arm64) inside the GPU
process, whose sandbox on Linux is the weaker kind. So the exposure is **page-supplied shaders reaching
that JIT compiler**, and exploiting it needs script already running in the app's page. That is
acceptable while the host shows only the app's own content: the page runs under `script-src 'self'` and
the host refuses navigation elsewhere (ADR-0003, [security-model.md](security-model.md)).

Three things to know:

- **It is not only the self-test.** The flag applies to every app on Linux and to every mode of the host.
  In `dev` the window loads Vite's own origin, which has no CSP, so "CSP-locked" is not true there; that
  is the developer's own machine and content.
- **It only permits a fallback.** On a machine with a working GPU nothing changes.
- **An app can refuse it:** `--no-software-webgl` (the host option) leaves the flag out, and WebGL then
  fails without a GPU, as in the table above. The default stays on. If the host ever shows content the
  app does not control, this decision must be made again.

The numbers with the flag, and for macOS and Windows, follow.

(ctest prints a passing test's output only when asked to, so each CEF host job re-runs the app-origin
self-test once with `-V` and prints the `graphics:` lines; that is where these numbers come from.
On your own machine: `ctest --preset cef -R host.self-test.app -V`.)

### With the flag, and on the other systems

CI run 42547109238 (the `graphics:` line of each CEF host job; the self-test with the app origin; one
run each, so read the frame rates as the order of magnitude, not as precise figures):

| | renderer | software? | WebGL, 20 000 moving points | 2D canvas, 300 moving circles |
|---|---|---|---|---|
| linux-x64, Xvfb, no GPU | WebGL 2 on ANGLE over Vulkan, **SwiftShader** (Subzero) | yes | 57.1 fps (59 frames, the slowest 67 ms) | 60.0 fps (slowest 17 ms) |
| linux-arm64, Xvfb, no GPU | WebGL 2 on ANGLE over Vulkan, **SwiftShader** (LLVM 10) | yes | 56.1 fps (58 frames, the slowest 83 ms) | 60.0 fps (slowest 17 ms) |
| macos-arm64, hosted runner | WebGL 2 on ANGLE over **Metal** ("Apple Paravirtual device") | no | 57.1 fps (the slowest 50 ms) | 60.0 fps (slowest 19 ms) |
| windows-x64, hosted runner | WebGL 2 on ANGLE over Direct3D 11, **Microsoft Basic Render Driver** (WARP) | yes | 63.0 fps (the slowest 31 ms) | 64.0 fps (slowest 16 ms) |
| windows-arm64, hosted runner | the same | yes | 61.2 fps (the slowest 59 ms) | 64.0 fps (slowest 16 ms) |

What this says:

- **With the flag, WebGL 2 works on both Linux targets without a GPU**, and it is the CPU doing it
  (`webgl.software=true`). The maximum texture size is 8192 everywhere.
- **The average is near 60 fps for this load, but the frames are not even**: the slowest frame in a
  second was 67 and 83 ms on Linux software rendering (four or five missed frames in a row), 31 to 59 ms on Windows
  WARP, 50 ms on the macOS runner. A map that redraws all of 20 000 points per frame is at the edge of
  what a CPU renderer does smoothly; a heavier scene will drop well below 60 on a machine with no GPU.
- **The 2D canvas is at 60 fps everywhere**, with 16 to 19 ms frames, for the instrument-like load.
- **The CI runners are not real GPUs** (software on Linux and Windows, a paravirtual GPU on macOS). A
  real GPU can only be better; none was measured. Read the `graphics:` line on a developer machine.
- Only the app origin was recorded; the https origin runs the same page and passes the same checks.

## What this means for the map

The map engine (#47 in the Mission Planner parity plan) is designed on three possibilities, and the
self-test tells which one holds on each target:

1. **WebGL on a GPU:** the full design (vector tiles or many sprites in WebGL).
2. **WebGL on the CPU (SwiftShader, llvmpipe):** works, at a frame rate far below a GPU's: the map needs a
   level-of-detail budget, and must not redraw the whole scene per frame.
3. **No WebGL:** a canvas-2D map with fewer features. This is a separate renderer; do not rely on it
   being cheap to add later.
