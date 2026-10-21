/**
 * Standalone Tobii Nexus webcam gaze sidecar (Node 20+).
 *
 * Hosts the Tobii Nexus Web WASM (`tobii_nexus_web`) and the `native-camera`
 * N-API addon in a plain Node process, and streams gaze to a parent process as
 * newline-delimited JSON on stdout. It is the source-available, Python-spawned
 * analogue of the demo-dashboard's Electron utilityProcess
 * (electron/services/tobiiSidecar/utility.ts) — the WASM host, browser shims,
 * fetch interceptor, and camera frame loop are lifted from there; the only
 * change is the IPC transport (Electron `parentPort` → stdio JSON-lines) and
 * the removal of the renderer-only paths (preview / face-frame / recording /
 * calibration).
 *
 * Protocol (one JSON object per line):
 *   stdin  ← {"type":"init", tobiiPackageDir, license, frameWidth, frameHeight,
 *             fps?, screenWidthMm, screenHeightMm, fovDeg, deviceId?}
 *           ← {"type":"destroy"}
 *   stdout → {"type":"started"}                       (on boot)
 *          → {"type":"ready"}                         (camera + WASM live)
 *          → {"type":"gaze", x, y, valid, timestamp, wallTime}
 *          → {"type":"fps", fps, sendFrameMs, res}
 *          → {"type":"error", msg}
 *
 * `x`/`y` are Tobii ADCS normalized screen coords in [0,1] (top-left origin);
 * the Python side scales them to pixels for the SDK's Nexus platform.
 */

import { readFileSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { pathToFileURL, fileURLToPath } from 'node:url'
import { createInterface } from 'node:readline'

const __dirname = dirname(fileURLToPath(import.meta.url))

// ---------------------------------------------------------------------------
// Browser-API shims required by Tobii's web-targeted Emscripten glue. The glue
// hardcodes ENVIRONMENT_IS_WEB=true and references document/self/navigator/
// XMLHttpRequest/fetch during evaluation AND during a second factory call from
// inside WebcamEyeTracker's constructor (which doesn't pass wasmBinary, so it
// falls through to a real fetch(nexus_wasm.wasm)). Install the same shim
// surface + a fetch interceptor that returns our pre-loaded wasm bytes.
// (Verbatim from utility.ts lines 44-110.)
// ---------------------------------------------------------------------------

if (typeof globalThis.document === 'undefined') {
  globalThis.document = { currentScript: { src: '' } }
}
if (typeof globalThis.self === 'undefined') globalThis.self = globalThis
if (typeof globalThis.window === 'undefined') globalThis.window = globalThis
if (typeof globalThis.navigator === 'undefined') {
  globalThis.navigator = new Proxy({}, { get() { return undefined } })
}
if (typeof globalThis.location === 'undefined') {
  globalThis.location = { href: 'file:///', origin: 'file://', protocol: 'file:' }
}
if (typeof globalThis.XMLHttpRequest === 'undefined') {
  class XhrShim {
    open() {}
    send() {}
    set onload(_) {}
    set onerror(_) {}
  }
  globalThis.XMLHttpRequest = XhrShim
}

let _cachedWasmBytes = null
let _cachedWasmDir = null
function loadCachedWasm() {
  if (_cachedWasmBytes) return _cachedWasmBytes
  if (!_cachedWasmDir) throw new Error('wasm bytes requested before _cachedWasmDir was set')
  _cachedWasmBytes = readFileSync(join(_cachedWasmDir, 'nexus_wasm.wasm'))
  return _cachedWasmBytes
}

const _realFetch = globalThis.fetch
globalThis.fetch = async (input, init) => {
  const url = typeof input === 'string' ? input : input?.url
  if (url && url.endsWith('nexus_wasm.wasm')) {
    const bytes = loadCachedWasm()
    const body = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    return new Response(body, { status: 200, headers: { 'content-type': 'application/wasm' } })
  }
  if (_realFetch) return _realFetch(input, init)
  throw new Error(`[webcam-sidecar] fetch shim: unhandled url=${url}`)
}

// ---------------------------------------------------------------------------
// IPC — stdio JSON-lines (replaces Electron parentPort).
// ---------------------------------------------------------------------------

function emit(msg) {
  try { process.stdout.write(JSON.stringify(msg) + '\n') } catch { /* parent gone */ }
}

// Diagnostics go to stderr so they never corrupt the stdout JSON stream.
function diag(...args) {
  const line = `[webcam-sidecar:diag] ${args.map((a) =>
    typeof a === 'string' ? a : a instanceof Error ? `${a.name}: ${a.message}` : JSON.stringify(a)
  ).join(' ')}\n`
  try { process.stderr.write(line) } catch { /* ignore */ }
}
function logErr(...args) { diag('error:', ...args) }

// Robust stringify — Tobii's Emscripten glue can throw non-Error values
// (numbers, abort objects, strings) with no `.message`, which would otherwise
// surface as "undefined".
function errStr(err) {
  if (err instanceof Error) return `${err.name}: ${err.message}${err.stack ? '\n' + err.stack : ''}`
  if (typeof err === 'object' && err !== null) {
    try { return JSON.stringify(err) } catch { return Object.prototype.toString.call(err) }
  }
  return String(err)
}

process.on('unhandledRejection', (err) => {
  diag('UNHANDLED REJECTION:', err instanceof Error ? `${err.name}: ${err.message}\n${err.stack ?? ''}` : String(err))
})
process.on('uncaughtException', (err) => {
  diag('UNCAUGHT EXCEPTION:', `${err.name}: ${err.message}\n${err.stack ?? ''}`)
})

diag(`boot: pid=${process.pid} node=${process.version} __dirname=${__dirname}`)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let tracker = null
let camera = null
let nativeCamera = null
let captureWidth = 0
let captureHeight = 0

// ---------------------------------------------------------------------------
// native-camera load — resolve the bundled addon next to this file.
// ---------------------------------------------------------------------------

async function loadNativeCamera() {
  if (nativeCamera) return nativeCamera
  const candidates = [
    join(__dirname, 'native-camera'),                          // bundled next to sidecar.mjs
    join(__dirname, 'node_modules', '@theia', 'native-camera'), // npm-installed dep
  ]
  for (const dir of candidates) {
    try {
      const indexUrl = pathToFileURL(join(dir, 'index.js')).href
      diag(`loadNativeCamera: import(${indexUrl})`)
      const mod = await import(indexUrl)
      const resolved = mod.default ?? mod
      if (resolved && typeof resolved.Camera === 'function') {
        nativeCamera = resolved
        return resolved
      }
    } catch (e) {
      diag(`loadNativeCamera: ${dir} threw: ${e.message}`)
    }
  }
  throw new Error('native-camera module not found')
}

// ---------------------------------------------------------------------------
// Tobii WASM init — pre-instantiate the factory with raw wasm bytes, construct
// WebcamEyeTracker, subscribe to gaze. (Adapted from utility.ts initTracker.)
// ---------------------------------------------------------------------------

async function initTracker(msg) {
  const distDir = msg.tobiiPackageDir
  if (!distDir) throw new Error('init.tobiiPackageDir is required')
  diag(`initTracker: distDir=${distDir} license=${(msg.license || '').length}ch ` +
    `screen=${msg.screenWidthMm}x${msg.screenHeightMm}mm fov=${msg.fovDeg} dim=${msg.frameWidth}x${msg.frameHeight}`)

  _cachedWasmDir = distDir
  const wasmBytes = loadCachedWasm()
  diag(`initTracker: wasm bytes=${wasmBytes.length}`)

  diag('initTracker: importing glue modules')
  const factoryMod = await import(pathToFileURL(join(distDir, 'nexus_wasm.js')).href)
  const wetMod = await import(pathToFileURL(join(distDir, 'WebcamEyeTracker.js')).href)
  const typesMod = await import(pathToFileURL(join(distDir, 'types.js')).href)

  // Force V8's sync-compile (TurboFan up front) instead of streaming-compile.
  diag('initTracker: pre-instantiating WASM factory')
  await factoryMod.default({
    wasmBinary: wasmBytes,
    locateFile: (p) => join(distDir, p),
  })
  diag('initTracker: factory done; constructing WebcamEyeTracker')

  tracker = new wetMod.WebcamEyeTracker(
    msg.frameWidth,
    msg.frameHeight,
    typesMod.FrameFormat.GRAYSCALE,
    msg.screenWidthMm,
    msg.screenHeightMm,
    msg.fovDeg,
    msg.license,
  )
  diag('initTracker: constructed; awaiting tracker.ready')
  await tracker.ready
  diag('initTracker: tracker.ready resolved')

  let firstGazeLogged = false
  tracker.subscribeToGaze((gaze) => {
    try {
      if (!firstGazeLogged) {
        diag(`gaze callback: first gaze x=${gaze.x} y=${gaze.y} valid=${gaze.valid}`)
        firstGazeLogged = true
      }
      emit({ type: 'gaze', x: gaze.x, y: gaze.y, valid: gaze.valid, timestamp: gaze.timestamp, wallTime: Date.now() })
    } catch (err) {
      diag(`gaze callback threw: ${err.message}`)
    }
  })
}

// ---------------------------------------------------------------------------
// Camera frame loop — feed the grayscale Y plane to the tracker. First-frame
// watchdog surfaces "camera opened but delivers no frames" as an error instead
// of a silent hang. (Adapted from utility.ts startCamera; preview/face-frame/
// recording/calibration removed.)
// ---------------------------------------------------------------------------

const FIRST_FRAME_TIMEOUT_MS = 5000

async function startCamera(opts) {
  captureWidth = opts.width
  captureHeight = opts.height
  if (camera) { try { camera.stop() } catch { /* ignore */ } camera = null }

  const mod = await loadNativeCamera()
  camera = new mod.Camera()

  const startOpts = { width: opts.width, height: opts.height, fps: opts.fps, color: true }
  if (typeof opts.deviceId === 'string' && opts.deviceId.length > 0) startOpts.deviceId = opts.deviceId

  let firstFrameLogged = false
  let firstFrameResolve = null
  let firstFrameReject = null
  const firstFrame = new Promise((resolve, reject) => { firstFrameResolve = resolve; firstFrameReject = reject })

  let frameCount = 0
  let totalSendMs = 0
  let lastStatsAt = Date.now()

  camera.start(
    startOpts,
    (frame, _tsMs) => {
      try {
        if (!firstFrameLogged) {
          diag(`frame callback: first frame len=${frame.length}`)
          firstFrameLogged = true
          firstFrameResolve?.()
        }
        if (!tracker) return
        // color capture → NV12 (Y + interleaved CbCr, len = w*h*1.5). The tracker
        // wants only the leading grayscale Y plane (byte-identical to grayscale
        // capture). A grayscale build delivers exactly w*h → yPlane == frame.
        const yLen = captureWidth * captureHeight
        const isNV12 = yLen > 0 && frame.length >= yLen + (yLen >> 1) && frame.length < yLen * 2
        const yPlane = isNV12 ? frame.subarray(0, yLen) : frame
        const t0 = performance.now()
        try {
          // Tobii sendFrame signature is (data, options) — NOT (data, timestamp).
          tracker.sendFrame(yPlane, { timestamp: performance.now() })
        } catch (err) {
          diag(`sendFrame threw: ${err.message}`)
        }
        totalSendMs += performance.now() - t0
        frameCount++
        const now = Date.now()
        if (now - lastStatsAt >= 5000) {
          const fps = frameCount / ((now - lastStatsAt) / 1000)
          emit({ type: 'fps', fps, sendFrameMs: frameCount > 0 ? totalSendMs / frameCount : 0, res: `${opts.width}x${opts.height}` })
          frameCount = 0; totalSendMs = 0; lastStatsAt = now
        }
      } catch (err) {
        diag(`frame callback threw: ${err.message}\n${err.stack ?? ''}`)
      }
    },
    (errMsg) => {
      emit({ type: 'error', msg: `native-camera: ${errMsg}` })
      if (!firstFrameLogged) firstFrameReject?.(new Error(`native-camera: ${errMsg}`))
    },
  )

  let watchTimer
  try {
    await Promise.race([
      firstFrame,
      new Promise((_, reject) => {
        watchTimer = setTimeout(
          () => reject(new Error(`camera opened but delivered no frames within ${FIRST_FRAME_TIMEOUT_MS}ms`)),
          FIRST_FRAME_TIMEOUT_MS,
        )
      }),
    ])
    diag('startCamera: first frame confirmed')
  } catch (err) {
    try { camera?.stop() } catch { /* ignore */ }
    camera = null
    throw err
  } finally {
    if (watchTimer) clearTimeout(watchTimer)
  }
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

async function handleMessage(msg) {
  diag(`handleMessage: type=${msg?.type}`)
  switch (msg?.type) {
    case 'init':
      try {
        await initTracker(msg)
        await startCamera({
          deviceId: msg.deviceId,
          width: msg.frameWidth,
          height: msg.frameHeight,
          fps: msg.fps ?? 30,
        })
        emit({ type: 'ready' })
      } catch (err) {
        emit({ type: 'error', msg: `init failed: ${errStr(err)}` })
      }
      break

    case 'destroy':
      try { camera?.stop() } catch { /* ignore */ }
      try { tracker?.destroy?.() } catch { /* ignore */ }
      camera = null
      tracker = null
      process.exit(0)
      break

    default:
      diag(`unknown message type: ${msg?.type}`)
  }
}

const rl = createInterface({ input: process.stdin })
rl.on('line', (line) => {
  const trimmed = line.trim()
  if (!trimmed) return
  let msg
  try { msg = JSON.parse(trimmed) } catch (err) {
    diag(`bad JSON line: ${err.message}`)
    return
  }
  void handleMessage(msg).catch((err) => emit({ type: 'error', msg: `handleMessage threw: ${errStr(err)}` }))
})
rl.on('close', () => {
  try { camera?.stop() } catch { /* ignore */ }
  try { tracker?.destroy?.() } catch { /* ignore */ }
  process.exit(0)
})

emit({ type: 'started' })
