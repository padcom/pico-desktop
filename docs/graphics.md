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
- **Without one** (a VM, a container, CI under Xvfb): Chromium falls back to its software
  rasterizer (SwiftShader). WebGL works, if it works, on the CPU: the self-test tells whether the
  host's Chromium allows that (`webgl.api`, `webgl.software=true`), and how many frames per second it
  manages for the two loads above.
- **An app must not assume 60 fps.** Check `renderer` and the measured frame rate at start (the same
  `WEBGL_debug_renderer_info` call), and have a cheaper mode: fewer points, no antialiasing,
  or a 2D canvas.
- Context creation can fail (no GPU and no software fallback, a blocklisted driver): the app needs a
  message and a 2D fallback, never a blank window.
- The page runs under the production CSP (`script-src 'self'`): shaders are strings given to
  `gl.shaderSource`, which the policy doesn't cover, so WebGL needs no relaxation.

## What was measured, and where

Honest account of the state when this was written:

- **Measured on a real host: not yet, by the author.** The development machine (linux-arm64, no GPU,
  no CEF host build) cannot run `pico-desktop-host`; the code was typechecked, linted and the fixture
  built there, and nothing more.
- **Where it runs:** CI, in the jobs "CEF host, linux-x64" and "CEF host, linux-arm64" (a native runner
  each, under Xvfb, so software rendering), in the ctest `host.self-test.app` and `host.self-test.https`
  (once per origin, so twice per job). It also runs in the macOS and Windows jobs, where it only reports.
- **The first CI results** (renderer, `software`, frames per second) go in the section below once a run
  has produced them.
- **Real GPUs are not covered by CI.** On a developer machine with one, run `pnpm cef` and read the
  `graphics:` line.

## Results

_To be filled in from the first CI run of this self-test (the pull request that added it says where)._

## What this means for the map

The map engine (#47 in the Mission Planner parity plan) is designed on three possibilities, and the
self-test tells which one holds on each target:

1. **WebGL on a GPU:** the full design (vector tiles or many sprites in WebGL).
2. **WebGL on the CPU (SwiftShader, llvmpipe):** works, at a frame rate far below a GPU's: the map needs a
   level-of-detail budget, and must not redraw the whole scene per frame.
3. **No WebGL:** a canvas-2D map with fewer features. This is a separate renderer; do not rely on it
   being cheap to add later.
