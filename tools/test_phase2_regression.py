#!/usr/bin/env python3
"""Phase 2 regression tests — verify no framebuffer/touch in daemon, direct-open patch present."""

import sys, os, subprocess, struct
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

PASS = 0
FAIL = 0

def check(name, condition, detail=""):
    global PASS, FAIL
    if condition:
        print(f"  PASS: {name}")
        PASS += 1
    else:
        print(f"  FAIL: {name} {detail}")
        FAIL += 1

def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src_dir = os.path.join(repo, "src")
    
    print("=== Phase 2 Regression Tests ===\n")
    
    # 1. Daemon source has no framebuffer references
    print("[1] No framebuffer in daemon source")
    fb_refs = []
    for f in os.listdir(src_dir):
        if f.endswith((".c", ".h")):
            path = os.path.join(src_dir, f)
            with open(path) as fh:
                for i, line in enumerate(fh, 1):
                    if "/dev/fb0" in line and not line.strip().startswith("*"):
                        fb_refs.append(f"{f}:{i}: {line.strip()}")
    check("no /dev/fb0 in source", len(fb_refs) == 0, str(fb_refs[:3]))
    
    # 2. Daemon source has no touch injection references
    print("[2] No touch injection in daemon source")
    touch_refs = []
    for f in os.listdir(src_dir):
        if f.endswith((".c", ".h")):
            path = os.path.join(src_dir, f)
            with open(path) as fh:
                for i, line in enumerate(fh, 1):
                    stripped = line.strip()
                    if (("/dev/input/event1" in stripped or 
                         "emit_touch" in stripped or
                         "write_touch_tap" in stripped) and 
                        not stripped.startswith("*") and 
                        not stripped.startswith("/*")):
                        touch_refs.append(f"{f}:{i}: {stripped}")
    check("no touch injection in source", len(touch_refs) == 0, str(touch_refs[:3]))
    
    # 3. Direct-open bypass removed; arm-window config added
    print("[3] Arm-window runtime plumbing")
    sys.path.insert(0, os.path.join(repo, "tools"))
    try:
        import patch_hiby_player as ph
        check("direct-open patch removed", not hasattr(ph, "AUDIOBOOK_DIRECT_OPEN_PATCHES"))
        check("direct-open cave removed", not hasattr(ph, "AUDIOBOOK_DIRECT_OPEN_CAVE_OFFSET"))
    except ImportError as e:
        check("patcher imports", False, str(e))

    config_c = os.path.join(src_dir, "config.c")
    with open(config_c) as f:
        content = f.read()
    check("arm window env added", "AUDIOBOOK_ARM_WINDOW_MS" in content)
    check("arm poll env added", "AUDIOBOOK_ARM_POLL_MS" in content)
    check("direct-open disabled by default", "book_title_direct_open_enabled               = 0" in content)

    state_c = os.path.join(src_dir, "state.c")
    with open(state_c) as f:
        content = f.read()
    check("monotonic arm clock used", "CLOCK_MONOTONIC" in content)
    check("arm window deadline tracked", "book_title_arm_deadline_ms" in content)
    check("arm window polling tracked", "book_title_arm_next_poll_ms" in content)
    check("arm window burst present", "state_arm_window_burst" in content)
    
    # 4. State machine has new states
    print("[4] Event-driven state machine")
    state_h = os.path.join(src_dir, "state.h")
    with open(state_h) as f:
        content = f.read()
    check("STATE_IDLE defined", "STATE_IDLE" in content)
    check("STATE_BOOK_OPENED defined", "STATE_BOOK_OPENED" in content)
    check("STATE_TRACKING defined", "STATE_TRACKING" in content)
    check("STATE_BOOK_COMPLETED defined", "STATE_BOOK_COMPLETED" in content)
    
    # 5. Smart rewind config exists
    print("[5] Smart rewind configuration")
    config_h = os.path.join(src_dir, "config.h")
    with open(config_h) as f:
        content = f.read()
    check("smart_rewind_enabled field", "smart_rewind_enabled" in content)
    check("rewind_short_ms field", "rewind_short_ms" in content)
    check("rewind_medium_ms field", "rewind_medium_ms" in content)
    check("rewind_long_ms field", "rewind_long_ms" in content)
    
    # 6. Resume record has completed field
    print("[6] Completion tracking")
    resume_h = os.path.join(src_dir, "resume.h")
    with open(resume_h) as f:
        content = f.read()
    check("completed field in resume", "completed" in content)
    
    # 7. Daemon compiles
    print("[7] Daemon compiles")
    zig = "/home/yetisoldier/tools/zig/zig"
    if os.path.exists(zig):
        result = subprocess.run(
            [zig, "cc", "-target", "mipsel-linux-musleabi", "-Os", "-static", "-s", 
             "-o", "/tmp/test_regression_daemon"] + 
            [os.path.join(src_dir, f) for f in sorted(os.listdir(src_dir)) if f.endswith(".c")],
            capture_output=True, text=True, timeout=60
        )
        check("daemon compiles clean", result.returncode == 0, result.stderr[:200] if result.stderr else "")
        if os.path.exists("/tmp/test_regression_daemon"):
            size = os.path.getsize("/tmp/test_regression_daemon")
            check("binary under 150KB", size < 150000, f"size={size}")
            os.unlink("/tmp/test_regression_daemon")
    else:
        check("zig available", False, "zig not found")
    
    print(f"\n=== Results: {PASS} PASS, {FAIL} FAIL ===")
    return 1 if FAIL > 0 else 0

if __name__ == "__main__":
    sys.exit(main())
