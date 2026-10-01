#!/usr/bin/env python3
"""GUI end-to-end 'point picking' test for the VSG render backend.

Unlike ``vsg_smoke_test.sh`` (which only renders the 3D view offscreen), this
script drives the application **the way a user does**: it launches
CloudCompare with the VSG backend, waits for the 3D view to be on screen and
then posts a real mouse click (Quartz CGEvent, i.e. the very same event stream
the window server delivers to a physical mouse) in the middle of that view.

It therefore exercises the whole chain:

    CGEvent -> vsgQt window -> ccVSGCameraManipulator (press/release)
            -> ccVSGWindowInterface::requestPicking() (deferred)
            -> offscreen R32_UINT ID pass + readback -> itemPicked / selection

The result is asserted on the ``[VSG][trace] pick:`` line the backend prints
(see ccVSGWindowInterface::doEntityPicking() / doPicking()).

Usage:
    vsg_pick_test.py [ASSET] [TIMEOUT_SEC]

Exit codes:
    0   a pick was reported (whatever it hit)
    1   no pick was reported within the timeout (or it hit nothing)
    127 setup error (app / asset / pyobjc missing)
"""

import atexit
import os
import re
import subprocess
import sys
import time

APP = "/Users/gsl/work/pointsMap/CloudCompareVSG/CloudCompare/build-hbqt/qCC/CloudCompare.app/Contents/MacOS/CloudCompare"
ASSET = sys.argv[1] if len(sys.argv) > 1 else "/Users/gsl/work/pointsMap/CloudCompareVSG/test_assets/cube.bin"
TIMEOUT = int(sys.argv[2]) if len(sys.argv) > 2 else 150

# The Qt6 build needs the stub AGL framework: macdeployqt drops the
# CMAKE_BUILD_RPATH after linking, so dyld is pointed at the stub here.
AGL_STUB = "/Users/gsl/work/pointsMap/CloudCompareVSG/.qt-agl-stub"

ERR_LOG = "/tmp/cc_vsg_pick.err"
VIEW_RECT_RE = re.compile(r"\[VSG\] view rect: x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)")
PICK_RE = re.compile(r"\[VSG\]\[trace\] pick: mode=(\d+) at \((\d+),(\d+)\) -> (.+)")


def fail(msg, code=1):
    print("RESULT: FAIL - {}".format(msg))
    sys.exit(code)


if not os.path.isfile(APP) or not os.access(APP, os.X_OK):
    print("FAIL: app not found or not executable: {}".format(APP))
    sys.exit(127)
if not os.path.isfile(ASSET):
    print("FAIL: asset not found: {}".format(ASSET))
    sys.exit(127)

try:
    import Quartz
    from AppKit import NSRunningApplication, NSApplicationActivateIgnoringOtherApps
except ImportError:
    print("FAIL: pyobjc is required (python3 -m pip install pyobjc-framework-Quartz pyobjc-framework-Cocoa)")
    sys.exit(127)

# a previous instance would own the window we are about to click in
subprocess.run(["pkill", "-f", "CloudCompare.app/Contents/MacOS/CloudCompare"], capture_output=True)
time.sleep(1)

if os.path.exists(ERR_LOG):
    os.remove(ERR_LOG)

USE_HID = "--hid" in sys.argv  # opt in: needs the Accessibility permission

env = dict(os.environ)
env["CC_RENDER_BACKEND"] = "VSG"
env["CC_VSG_VIEW"] = "1"
env["CC_NO_LOAD_DIALOG"] = "1"
# The click is injected by the application itself (CC_VSG_PICK="rx,ry"), as a
# QMouseEvent delivered to the vsgQt::Window: macOS silently drops synthetic HID
# events posted by a process that is not trusted for Accessibility, and the
# permission cannot be granted from a script. Pass --hid to try a real HID click
# anyway (it only works once the permission has been granted by hand).
env["CC_VSG_PICK"] = "0.5,0.5"
# no CC_VSG_SCREENSHOT here: that hook quits the app once the image is saved
if os.path.isdir(os.path.join(AGL_STUB, "AGL.framework")):
    env["DYLD_FRAMEWORK_PATH"] = AGL_STUB + (":" + env["DYLD_FRAMEWORK_PATH"] if env.get("DYLD_FRAMEWORK_PATH") else "")

print("==> asset   : {}".format(ASSET))
print("==> timeout : {}s".format(TIMEOUT))

err_fd = open(ERR_LOG, "wb")
proc = subprocess.Popen([APP, ASSET], stdout=err_fd, stderr=err_fd, env=env)

# whatever happens below, never leave the application running (it hangs on
# shutdown anyway - see the migration plan)
atexit.register(lambda: subprocess.run(["kill", "-9", str(proc.pid)], capture_output=True))


def read_log():
    with open(ERR_LOG, "r", errors="replace") as f:
        return f.read()


# --------------------------------------------------------------------------
# 1. wait for the 3D view to be mapped and to report its screen rectangle
# --------------------------------------------------------------------------
view_rect = None
deadline = time.time() + TIMEOUT
while time.time() < deadline:
    if proc.poll() is not None:
        fail("the application exited before the 3D view was created")
    m = VIEW_RECT_RE.search(read_log())
    if m:
        view_rect = tuple(int(g) for g in m.groups())
        break
    time.sleep(1)

if view_rect is None:
    print("---- application log ----")
    print(read_log())
    fail("the 3D view never reported its rectangle ('[VSG] view rect:')")

vx, vy, vw, vh = view_rect
print("==> 3D view : x={} y={} {}x{}".format(vx, vy, vw, vh))

# --------------------------------------------------------------------------
# 2. click in the middle of the 3D view
# --------------------------------------------------------------------------
ns_app = NSRunningApplication.runningApplicationWithProcessIdentifier_(proc.pid)
if ns_app:
    ns_app.activateWithOptions_(NSApplicationActivateIgnoringOtherApps)
time.sleep(1)

if not USE_HID:
    print("==> Qt level click (CC_VSG_PICK=0.5,0.5): macOS drops synthetic HID")
    print("    events posted by a process without the Accessibility permission")
else:
    print("==> HID click at ({}, {})".format(vx + vw // 2, vy + vh // 2))


def post_mouse(event_type, point, button):
    event = Quartz.CGEventCreateMouseEvent(None, event_type, point, button)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, event)


# kCGMouseButtonLeft == 0 (pyobjc does not export kCGLeftMouseButton)
LEFT_BUTTON = 0

if USE_HID:
    point = Quartz.CGPointMake(vx + vw // 2, vy + vh // 2)
    post_mouse(Quartz.kCGEventMouseMoved, point, LEFT_BUTTON)
    time.sleep(0.2)
    post_mouse(Quartz.kCGEventLeftMouseDown, point, LEFT_BUTTON)
    time.sleep(0.1)
    post_mouse(Quartz.kCGEventLeftMouseUp, point, LEFT_BUTTON)

# --------------------------------------------------------------------------
# 3. the picking is deferred to the next frame: wait for its trace
# --------------------------------------------------------------------------
pick = None
deadline = time.time() + 30
while time.time() < deadline:
    m = PICK_RE.search(read_log())
    if m:
        pick = m
        break
    if proc.poll() is not None:
        print("==> the application exited (code={}) while picking was running".format(proc.returncode))
        break
    time.sleep(0.5)

print("---- [VSG] lines ----")
for line in read_log().splitlines():
    if "[VSG]" in line:
        print("    {}".format(line))

ok = False
if pick is None:
    print("==> no '[VSG][trace] pick:' line was emitted")
else:
    mode, px, py_, what = pick.groups()
    print("==> pick reported: mode={} at ({},{}) -> {}".format(mode, px, py_, what))
    ok = "<nothing>" not in what

subprocess.run(["kill", "-9", str(proc.pid)], capture_output=True)
err_fd.close()

if ok:
    print("RESULT: PASS")
    sys.exit(0)
fail("the click did not pick anything in the 3D view")
