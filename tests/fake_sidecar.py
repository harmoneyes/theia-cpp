#!/usr/bin/env python3
"""A stand-in for webcam-sidecar/sidecar.mjs.

Written from the real sidecar's contract, NOT from what the C++ happens to
send. That distinction matters: an earlier stub accepted whatever it was given
and so could never have caught a missing required field, which is exactly the
bug it failed to catch.

Mirrored from sidecar.mjs:
  - emits {"type":"started"} on boot                              (:359)
  - init.tobiiPackageDir is REQUIRED; without it the sidecar
    throws and the error surfaces as {"type":"error","msg":...}   (:167-168, :325)
  - emits {"type":"ready"} once initialised                       (:323)
  - gaze carries both `timestamp` (engine) and `wallTime`         (:209)
  - diagnostics go to stderr so stdout stays pure protocol

Options:
  --no-ready       accept init but never report readiness
  --die-after N    exit abruptly after N gaze messages
"""
import json, sys, threading, time

argv = sys.argv[1:]
no_ready  = "--no-ready" in argv
die_after = int(argv[argv.index("--die-after") + 1]) if "--die-after" in argv else None

def emit(obj):
    print(json.dumps(obj), flush=True)

def diag(text):
    sys.stderr.write(text + "\n"); sys.stderr.flush()

emit({"type": "started"})

raw = sys.stdin.readline()
if not raw:
    sys.exit(0)
try:
    msg = json.loads(raw)
except Exception as exc:
    emit({"type": "error", "msg": "handleMessage threw: %s" % exc}); sys.exit(1)

if msg.get("type") != "init":
    emit({"type": "error", "msg": "init failed: expected an init message"}); sys.exit(1)

# The check that the real sidecar performs first.
if not msg.get("tobiiPackageDir"):
    emit({"type": "error", "msg": "init failed: Error: init.tobiiPackageDir is required"})
    sys.exit(1)

diag("initTracker: distDir=%s license=%dch screen=%sx%smm fov=%s dim=%sx%s" % (
    msg.get("tobiiPackageDir"), len(msg.get("license") or ""),
    msg.get("screenWidthMm"), msg.get("screenHeightMm"), msg.get("fovDeg"),
    msg.get("frameWidth"), msg.get("frameHeight")))

if no_ready:
    while True:
        time.sleep(0.05)

emit({"type": "ready"})

def drain():
    for line in sys.stdin:
        if '"destroy"' in line:
            emit({"type": "shutdown"})
            sys.exit(0)
threading.Thread(target=drain, daemon=True).start()

wall = time.time() * 1000.0
sent = 0
for i in range(10_000):
    emit({"type": "gaze", "x": 0.25, "y": 0.75, "valid": True,
          "timestamp": wall + i * 33.3, "wallTime": time.time() * 1000.0})
    sent += 1
    if die_after is not None and sent >= die_after:
        sys.stderr.write("stub: dying abruptly\n")
        import os; os._exit(1)
    time.sleep(0.004)
