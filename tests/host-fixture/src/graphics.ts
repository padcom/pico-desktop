// Graphics: can the page draw with WebGL and a 2D canvas in this host, with which renderer, and do
// animation frames arrive? The map (WebGL) and the instruments (canvas) of an app depend on it
// (docs/graphics.md). On Linux, two things fail the self-test: no WebGL context at all, and frames
// that don't arrive. How fast they arrive and which renderer draws them are reported as `pd-info:`
// lines, not asserted: they depend on the machine, and under Xvfb in CI there is no GPU.
import { info } from './probes'
import type { NamedCheck, Outcome } from './report'

type Gl = WebGL2RenderingContext | WebGLRenderingContext

/** A WebGL renderer whose name says it runs on the CPU. */
const SOFTWARE = /swiftshader|llvmpipe|softpipe|lavapipe|software|basic render/iu

/** Fewer frames per second than this means the compositor isn't delivering frames at all. */
const MIN_FPS = 10

const MEASURE_MS = 1000

// A map-like load: 20 000 points that move a little every frame. GLSL ES 1.00 runs in WebGL 1 and 2.
const VERTEX = 'attribute vec2 p; uniform float t; '
  + 'void main() { gl_Position = vec4(p + 0.05 * vec2(sin(t), cos(t)), 0.0, 1.0); gl_PointSize = 3.0; }'
const FRAGMENT = 'precision mediump float; void main() { gl_FragColor = vec4(0.2, 0.6, 1.0, 1.0); }'
const POINTS = 20_000

function newCanvas(): HTMLCanvasElement {
  const canvas = document.createElement('canvas')
  canvas.width = 800
  canvas.height = 600
  document.body.append(canvas)

  return canvas
}

// WebGL 2 if there is one, else WebGL 1 (the context is taken from the canvas, never both).
function webgl(canvas: HTMLCanvasElement): { api: string, gl: Gl } | null {
  const gl2 = canvas.getContext('webgl2')
  const gl = gl2 ?? canvas.getContext('webgl')

  return gl ? { api: gl2 ? 'webgl2' : 'webgl', gl } : null
}

function rendererOf(gl: Gl): { vendor: string, renderer: string } {
  // The unmasked strings name the real GPU or software renderer; without the extension, the generic ones.
  const debug = gl.getExtension('WEBGL_debug_renderer_info')

  return {
    vendor: String(gl.getParameter(debug ? debug.UNMASKED_VENDOR_WEBGL : gl.VENDOR)),
    renderer: String(gl.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : gl.RENDERER)),
  }
}

function reportRenderer(api: string, gl: Gl): void {
  const { vendor, renderer } = rendererOf(gl)
  info('webgl.api', api)
  info('webgl.renderer', renderer)
  info('webgl.vendor', vendor)
  info('webgl.version', String(gl.getParameter(gl.VERSION)))
  info('webgl.software', String(SOFTWARE.test(renderer)))
  info('webgl.maxTextureSize', String(gl.getParameter(gl.MAX_TEXTURE_SIZE)))
}

// WebGL is required on Linux (x64 and arm64: the gates of docs/graphics.md). Elsewhere a missing
// context is reported, not failed: those systems aren't gates yet, and what they have is evidence.
const REQUIRED = navigator.userAgent.includes('Linux')

function noWebgl(): Outcome {
  info('webgl.api', 'none')

  return !REQUIRED || 'neither a WebGL 2 nor a WebGL 1 context could be created (no GPU and no software fallback?)'
}

function webglCreated(): Outcome {
  const found = webgl(newCanvas())
  if (!found) {
    return noWebgl()
  }
  reportRenderer(found.api, found.gl)

  return true
}

function compile(gl: Gl, type: number, source: string): WebGLShader {
  const shader = gl.createShader(type)
  if (!shader) {
    throw new Error('no shader')
  }
  gl.shaderSource(shader, source)
  gl.compileShader(shader)

  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    throw new Error(gl.getShaderInfoLog(shader) ?? 'shader compile error')
  }

  return shader
}

// Draws the points, a little differently every frame.
function pointsDrawing(gl: Gl): (frame: number) => void {
  const program = gl.createProgram()
  gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, VERTEX))
  gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, FRAGMENT))
  gl.linkProgram(program)
  gl.useProgram(program)
  const positions = Float32Array.from({ length: POINTS * 2 }, (_, i) => Math.sin(i * 12.9898) % 1)
  gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer())
  gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW)
  const at = gl.getAttribLocation(program, 'p')
  gl.enableVertexAttribArray(at)
  gl.vertexAttribPointer(at, 2, gl.FLOAT, false, 0, 0)
  const time = gl.getUniformLocation(program, 't')

  return frame => {
    gl.uniform1f(time, frame / 10)
    gl.clear(gl.COLOR_BUFFER_BIT)
    gl.drawArrays(gl.POINTS, 0, POINTS)
  }
}

// An instrument-like load: 300 circles that move every frame.
function circlesDrawing(context: CanvasRenderingContext2D): (frame: number) => void {
  return frame => {
    context.clearRect(0, 0, 800, 600)
    context.beginPath()

    for (let i = 0; i < 300; i += 1) {
      context.moveTo(400 + (i * 2), 300 + (Math.sin((frame / 20) + i) * 200))
      context.arc(400 + ((i - 150) * 2), 300 + (Math.sin((frame / 20) + i) * 200), 6, 0, Math.PI * 2)
    }
    context.fill()
  }
}

interface Frames {
  frames: number
  fps: number
  worstMs: number
}

function summarize(times: number[]): Frames {
  const spans = times.slice(1).map((time, i) => time - (times[i] ?? time))
  const seconds = ((times.at(-1) ?? 0) - (times[0] ?? 0)) / 1000

  return { frames: times.length, fps: seconds > 0 ? (times.length - 1) / seconds : 0, worstMs: Math.max(0, ...spans) }
}

// Draws on every animation frame for `MEASURE_MS` and says how many frames came and how evenly.
function measureFrames(draw: (frame: number) => void): Promise<Frames> {
  return new Promise(resolve => {
    const times: number[] = []
    const step = (now: number) => {
      times.push(now)
      draw(times.length)

      if (now - (times[0] ?? now) < MEASURE_MS) {
        requestAnimationFrame(step)
      } else {
        resolve(summarize(times))
      }
    }
    requestAnimationFrame(step)
  })
}

function reportFrames(name: string, result: Frames): Outcome {
  info(`${name}.fps`, `${result.fps.toFixed(1)} (${result.frames} frames, the slowest ${result.worstMs.toFixed(0)} ms)`)

  return result.fps >= MIN_FPS || !REQUIRED || `only ${result.fps.toFixed(1)} frames per second`
}

async function animatedWebgl(): Promise<Outcome> {
  const found = webgl(newCanvas())
  if (!found) {
    return !REQUIRED || 'no WebGL context'
  }

  return reportFrames('webgl', await measureFrames(pointsDrawing(found.gl)))
}

async function animated2d(): Promise<Outcome> {
  const context = newCanvas().getContext('2d')
  if (!context) {
    return 'no 2D context'
  }

  return reportFrames('canvas2d', await measureFrames(circlesDrawing(context)))
}

export const graphics: NamedCheck[] = [
  ['WebGL context is created', webglCreated],
  ['an animated WebGL canvas gets frames', animatedWebgl],
  ['an animated 2D canvas gets frames', animated2d],
]
